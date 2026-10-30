#include "audio/native_audio_backend.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef LIBRESHOCKWAVE_DEBUGGER_HAS_AUDIO
#include <dlfcn.h>
#include <mpg123.h>
#endif

#include "libreshockwave/player/audio/audio_backend.hpp"

namespace libreshockwave::debugger {
namespace {

#ifdef LIBRESHOCKWAVE_DEBUGGER_HAS_AUDIO

constexpr int kMaxChannels = 8;
constexpr int kSampleRate = 44100;
constexpr std::size_t kChannelsPerFrame = 2;
constexpr std::size_t kFramesPerBuffer = 1024;

std::uint16_t readLe16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool hasBytes(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t count) {
    return offset <= data.size() && count <= data.size() - offset;
}

std::int16_t readPcm16(const std::uint8_t* data, std::size_t sample_index, std::size_t channel,
                       std::size_t channels) {
    const std::size_t offset = (sample_index * channels + channel) * 2;
    const auto value = static_cast<std::uint16_t>(data[offset]) |
                       static_cast<std::uint16_t>(data[offset + 1] << 8);
    return static_cast<std::int16_t>(value);
}

bool decodeWav(const std::vector<std::uint8_t>& bytes, std::vector<std::int16_t>& samples) {
    if (!hasBytes(bytes, 0, 12) || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint32_t sample_rate = 0;
    const std::uint8_t* audio_data = nullptr;
    std::size_t audio_size = 0;

    std::size_t offset = 12;
    while (hasBytes(bytes, offset, 8)) {
        const std::uint32_t chunk_size = readLe32(bytes, offset + 4);
        const std::size_t body = offset + 8;
        if (!hasBytes(bytes, body, chunk_size)) {
            break;
        }
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format = readLe16(bytes, body);
            channels = readLe16(bytes, body + 2);
            sample_rate = readLe32(bytes, body + 4);
            bits_per_sample = readLe16(bytes, body + 14);
        } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
            audio_data = bytes.data() + body;
            audio_size = chunk_size;
        }
        offset = body + chunk_size + (chunk_size & 1U);
    }

    if (audio_format != 1 || bits_per_sample != 16 || (channels != 1 && channels != 2) ||
        sample_rate == 0 || audio_data == nullptr) {
        return false;
    }

    const std::size_t frame_count = audio_size / (channels * 2);
    if (frame_count == 0) {
        return false;
    }

    const double ratio = static_cast<double>(sample_rate) / kSampleRate;
    const std::size_t output_frames = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(frame_count / ratio)));
    samples.resize(output_frames * kChannelsPerFrame);

    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        const double source_frame = static_cast<double>(frame) * ratio;
        const std::size_t source_index = std::min(
            static_cast<std::size_t>(source_frame), frame_count - 1);
        const std::size_t next_index = std::min(source_index + 1, frame_count - 1);
        const double fraction = source_frame - static_cast<double>(source_index);
        for (std::size_t channel = 0; channel < kChannelsPerFrame; ++channel) {
            const std::size_t source_channel = std::min(channel, static_cast<std::size_t>(channels - 1));
            const double first = readPcm16(audio_data, source_index, source_channel, channels);
            const double second = readPcm16(audio_data, next_index, source_channel, channels);
            const double value = first + (second - first) * fraction;
            samples[frame * kChannelsPerFrame + channel] = static_cast<std::int16_t>(
                std::clamp(std::lround(value), -32768L, 32767L));
        }
    }
    return true;
}

bool decodeMp3(const std::vector<std::uint8_t>& bytes, std::vector<std::int16_t>& samples) {
    mpg123_handle* handle = mpg123_new(nullptr, nullptr);
    if (handle == nullptr) {
        return false;
    }
    if (mpg123_format_none(handle) != MPG123_OK ||
        mpg123_format(handle, kSampleRate, MPG123_STEREO, MPG123_ENC_SIGNED_16) != MPG123_OK ||
        mpg123_open_feed(handle) != MPG123_OK) {
        mpg123_delete(handle);
        return false;
    }

    std::array<std::int16_t, 8192> buffer{};
    auto append = [&](std::size_t done) {
        if (done == 0) {
            return;
        }
        if (done % 2 != 0) {
            return;
        }
        const auto count = done / sizeof(std::int16_t);
        samples.insert(samples.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
    };

    std::size_t done = 0;
    int result = mpg123_decode(handle,
                               bytes.data(),
                               bytes.size(),
                               reinterpret_cast<unsigned char*>(buffer.data()),
                               buffer.size() * sizeof(std::int16_t),
                               &done);
    append(done);

    while (result == MPG123_OK || result == MPG123_NEW_FORMAT) {
        done = 0;
        result = mpg123_decode(handle,
                               nullptr,
                               0,
                               reinterpret_cast<unsigned char*>(buffer.data()),
                               buffer.size() * sizeof(std::int16_t),
                               &done);
        append(done);
    }

    mpg123_delete(handle);
    return result == MPG123_DONE || result == MPG123_NEED_MORE;
}

class AlsaOutput {
public:
    ~AlsaOutput() { close(); }

    [[nodiscard]] bool isOpen() const { return pcm_ != nullptr; }

    bool open() {
        close();
        library_ = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (library_ == nullptr) {
            return false;
        }
        open_ = load<OpenFn>("snd_pcm_open");
        set_params_ = load<SetParamsFn>("snd_pcm_set_params");
        write_ = load<WriteFn>("snd_pcm_writei");
        recover_ = load<RecoverFn>("snd_pcm_recover");
        close_ = load<CloseFn>("snd_pcm_close");
        if (open_ == nullptr || set_params_ == nullptr || write_ == nullptr ||
            recover_ == nullptr || close_ == nullptr) {
            close();
            return false;
        }

        Pcm* pcm = nullptr;
        if (open_(&pcm, "default", 0, 0) < 0 || pcm == nullptr) {
            close();
            return false;
        }
        if (set_params_(pcm, kFormatS16Le, kAccessRwInterleaved, 2, kSampleRate, 1, 100000) < 0) {
            close_(pcm);
            close();
            return false;
        }
        pcm_ = pcm;
        return true;
    }

    bool write(const std::int16_t* samples, std::size_t frames) {
        if (pcm_ == nullptr || write_ == nullptr) {
            return false;
        }
        std::size_t written_frames = 0;
        while (written_frames < frames) {
            const auto written = write_(
                pcm_, samples + written_frames * kChannelsPerFrame, frames - written_frames);
            if (written < 0) {
                if (recover_(pcm_, static_cast<int>(written), 1) < 0) {
                    return false;
                }
                continue;
            }
            if (written == 0) {
                return false;
            }
            written_frames += static_cast<std::size_t>(written);
        }
        return true;
    }

    void close() {
        if (pcm_ != nullptr && close_ != nullptr) {
            close_(pcm_);
            pcm_ = nullptr;
        }
        if (library_ != nullptr) {
            dlclose(library_);
            library_ = nullptr;
        }
        open_ = nullptr;
        set_params_ = nullptr;
        write_ = nullptr;
        recover_ = nullptr;
        close_ = nullptr;
    }

private:
    struct Pcm;
    using OpenFn = int (*)(Pcm**, const char*, int, int);
    using SetParamsFn = int (*)(Pcm*, int, int, unsigned int, unsigned int, int, unsigned int);
    using WriteFn = long (*)(Pcm*, const void*, unsigned long);
    using RecoverFn = int (*)(Pcm*, int, int);
    using CloseFn = int (*)(Pcm*);

    static constexpr int kFormatS16Le = 2;
    static constexpr int kAccessRwInterleaved = 3;

    template <typename T>
    T load(const char* name) {
        return reinterpret_cast<T>(dlsym(library_, name));
    }

    void* library_{nullptr};
    Pcm* pcm_{nullptr};
    OpenFn open_{nullptr};
    SetParamsFn set_params_{nullptr};
    WriteFn write_{nullptr};
    RecoverFn recover_{nullptr};
    CloseFn close_{nullptr};
};

struct Clip {
    std::vector<std::int16_t> samples;
    std::size_t position{0};
    int remaining_loops{1};
    std::uint64_t elapsed_frames{0};
    bool active{false};
};

class NativeAudioBackend final : public player::audio::AudioBackend {
public:
    NativeAudioBackend() {
        mpg123_init();
        for (int channel = 1; channel <= kMaxChannels; ++channel) {
            playing_[channel].store(false);
            volumes_[channel].store(255);
        }
        worker_ = std::thread([this] { run(); });
    }

    ~NativeAudioBackend() override {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        mpg123_exit();
    }

    void play(int channel_num,
              const std::vector<std::uint8_t>& audio_data,
              std::string_view format,
              int loop_count) override {
        if (!isValidChannel(channel_num) || audio_data.empty()) {
            return;
        }

        std::vector<std::int16_t> samples;
        const bool decoded = format == "wav" ? decodeWav(audio_data, samples)
                                             : decodeMp3(audio_data, samples);
        if (!decoded || samples.empty()) {
            std::cerr << "debugger audio: cannot decode " << format << " clip on channel "
                      << channel_num << '\n';
            return;
        }

        {
            std::lock_guard lock(mutex_);
            Clip& clip = clips_[channel_num];
            clip.samples = std::move(samples);
            clip.position = 0;
            clip.remaining_loops = std::max(0, loop_count);
            clip.elapsed_frames = 0;
            clip.active = true;
            playing_[channel_num].store(true);
        }
        condition_.notify_one();
    }

    void stop(int channel_num) override {
        if (!isValidChannel(channel_num)) {
            return;
        }
        std::lock_guard lock(mutex_);
        clips_[channel_num].active = false;
        clips_[channel_num].samples.clear();
        clips_[channel_num].position = 0;
        playing_[channel_num].store(false);
    }

    void stopAll() override {
        std::lock_guard lock(mutex_);
        for (int channel = 1; channel <= kMaxChannels; ++channel) {
            clips_[channel].active = false;
            clips_[channel].samples.clear();
            clips_[channel].position = 0;
            playing_[channel].store(false);
        }
    }

    void setVolume(int channel_num, int volume) override {
        if (isValidChannel(channel_num)) {
            volumes_[channel_num].store(std::clamp(volume, 0, 255));
        }
    }

    [[nodiscard]] bool isPlaying(int channel_num) const override {
        return isValidChannel(channel_num) && playing_[channel_num].load();
    }

    [[nodiscard]] int getElapsedTime(int channel_num) const override {
        if (!isValidChannel(channel_num)) {
            return 0;
        }
        std::lock_guard lock(mutex_);
        const auto frames = clips_[channel_num].elapsed_frames;
        const auto milliseconds = frames * 1000 / kSampleRate;
        return static_cast<int>(
            std::min<std::uint64_t>(milliseconds, std::numeric_limits<int>::max()));
    }

private:
    [[nodiscard]] static bool isValidChannel(int channel_num) {
        return channel_num >= 1 && channel_num <= kMaxChannels;
    }

    [[nodiscard]] bool anyActive() const {
        return std::any_of(clips_.begin() + 1, clips_.end(), [](const Clip& clip) {
            return clip.active;
        });
    }

    void deactivateAllLocked() {
        for (int channel = 1; channel <= kMaxChannels; ++channel) {
            clips_[channel].active = false;
            clips_[channel].samples.clear();
            playing_[channel].store(false);
        }
    }

    void run() {
        AlsaOutput output;
        bool output_error_logged = false;
        std::array<std::int32_t, kFramesPerBuffer * kChannelsPerFrame> mix{};
        std::array<std::int16_t, kFramesPerBuffer * kChannelsPerFrame> pcm{};

        while (true) {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return quit_ || anyActive(); });
            if (quit_) {
                return;
            }

            if (!output.isOpen() && !output.open()) {
                if (!output_error_logged) {
                    std::cerr << "debugger audio: cannot open the default output device; "
                                 "sound is disabled\n";
                    output_error_logged = true;
                }
                deactivateAllLocked();
                continue;
            }

            mix.fill(0);
            for (int channel = 1; channel <= kMaxChannels; ++channel) {
                Clip& clip = clips_[channel];
                if (!clip.active) {
                    continue;
                }
                const int volume = volumes_[channel].load();
                for (std::size_t frame = 0; frame < kFramesPerBuffer; ++frame) {
                    if (clip.position + 1 >= clip.samples.size()) {
                        if (clip.remaining_loops == 0) {
                            clip.position = 0;
                        } else if (clip.remaining_loops > 1) {
                            --clip.remaining_loops;
                            clip.position = 0;
                        } else {
                            clip.active = false;
                            playing_[channel].store(false);
                            break;
                        }
                    }
                    mix[frame * kChannelsPerFrame] +=
                        static_cast<std::int32_t>(clip.samples[clip.position]) * volume / 255;
                    mix[frame * kChannelsPerFrame + 1] +=
                        static_cast<std::int32_t>(clip.samples[clip.position + 1]) * volume / 255;
                    clip.position += kChannelsPerFrame;
                    ++clip.elapsed_frames;
                }
            }

            for (std::size_t index = 0; index < mix.size(); ++index) {
                pcm[index] = static_cast<std::int16_t>(std::clamp(mix[index], -32768, 32767));
            }
            lock.unlock();

            if (!output.write(pcm.data(), kFramesPerBuffer)) {
                if (!output_error_logged) {
                    std::cerr << "debugger audio: output device failed; sound is disabled\n";
                    output_error_logged = true;
                }
                output.close();
                std::lock_guard lock(mutex_);
                deactivateAllLocked();
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::array<Clip, kMaxChannels + 1> clips_{};
    std::array<std::atomic<bool>, kMaxChannels + 1> playing_{};
    std::array<std::atomic<int>, kMaxChannels + 1> volumes_{};
    bool quit_{false};
    std::thread worker_;
};

#endif

} // namespace

std::unique_ptr<player::audio::AudioBackend> createDebuggerAudioBackend() {
#ifdef LIBRESHOCKWAVE_DEBUGGER_HAS_AUDIO
    return std::make_unique<NativeAudioBackend>();
#else
    return nullptr;
#endif
}

} // namespace libreshockwave::debugger
