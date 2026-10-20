#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/audio/audio_backend.hpp"
#include "libreshockwave/player/audio/sound_queue.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class CastMemberChunk;
class SoundChunk;
}

namespace libreshockwave::player::audio {

class SoundManager {
public:
    static constexpr int MAX_CHANNELS = 8;
    // Director's default when fadeIn/fadeOut/fadeTo get no duration.
    static constexpr int DEFAULT_FADE_MS = 1000;
    using Clock = std::function<std::int64_t()>;

    using AudioResolver = std::function<std::optional<std::vector<std::uint8_t>>(
        const lingo::Datum::CastMemberRef&)>;

    SoundManager();
    explicit SoundManager(DirectorFile* source_file);

    void setBackend(AudioBackend* backend);
    [[nodiscard]] AudioBackend* backend() const;

    void setAudioResolver(AudioResolver resolver);
    void setCastLibSourceFile(int cast_lib, DirectorFile* source_file);
    [[nodiscard]] DirectorFile* castLibSourceFile(int cast_lib) const;

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;
    void setSoundLevel(int level);
    [[nodiscard]] int getSoundLevel() const;
    void setSoundKeepDevice(bool keep_device);
    [[nodiscard]] bool soundKeepDevice() const;
    void setSoundMixMedia(bool mix_media);
    [[nodiscard]] bool soundMixMedia() const;
    void play(int channel_num, const lingo::Datum& args);
    // play() with no member: starts the channel's queue. The first entry plays at
    // once unless a clip is already playing, and updateQueues() plays the rest.
    void play(int channel_num);
    void queue(int channel_num, const lingo::Datum& args);
    // Interrupts the current clip with the next queued entry and runs the queue.
    void playNext(int channel_num);
    // Plays the next entry on every running queue whose clip has ended. Called once
    // per frame; a queue with nothing left stops running.
    void updateQueues();
    void stop(int channel_num);
    void stopAll();
    void setVolume(int channel_num, int volume);
    // Fades ramp the channel volume linearly; updateFades() advances them from the
    // clock. fadeOut stops the channel at silence; setVolume, play and stop cancel.
    void setClock(Clock clock);
    void fadeIn(int channel_num, int milliseconds);
    void fadeOut(int channel_num, int milliseconds);
    void fadeTo(int channel_num, int volume, int milliseconds);
    void updateFades();
    [[nodiscard]] bool isFading(int channel_num) const;
    [[nodiscard]] int getVolume(int channel_num) const;
    void setLoopCount(int channel_num, int loop_count);
    [[nodiscard]] int getLoopCount(int channel_num) const;
    void setPan(int channel_num, int pan);
    [[nodiscard]] int getPan(int channel_num) const;
    void setStartTime(int channel_num, int start_time);
    [[nodiscard]] int getStartTime(int channel_num) const;
    void setEndTime(int channel_num, int end_time);
    [[nodiscard]] int getEndTime(int channel_num) const;
    void setLoopStartTime(int channel_num, int loop_start_time);
    [[nodiscard]] int getLoopStartTime(int channel_num) const;
    void setLoopEndTime(int channel_num, int loop_end_time);
    [[nodiscard]] int getLoopEndTime(int channel_num) const;
    void setMember(int channel_num, const lingo::Datum::CastMemberRef& member_ref);
    [[nodiscard]] std::optional<lingo::Datum::CastMemberRef> getMember(int channel_num) const;
    void setPlaylist(int channel_num, const lingo::Datum& playlist);
    [[nodiscard]] std::vector<lingo::Datum> getPlaylist(int channel_num) const;
    [[nodiscard]] bool isPlaying(int channel_num) const;
    // Director's isBusy(): playing, or a running queue still has entries waiting.
    [[nodiscard]] bool isBusy(int channel_num) const;
    [[nodiscard]] int getElapsedTime(int channel_num) const;

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> resolveAudioData(const lingo::Datum& member_ref) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> resolveAudioData(
        const lingo::Datum::CastMemberRef& member_ref) const;

    [[nodiscard]] static bool isValidChannel(int channel_num);
    [[nodiscard]] static int clampVolume(int volume);
    [[nodiscard]] static int clampLoopCount(int loop_count);
    [[nodiscard]] static std::string_view detectFormat(const std::vector<std::uint8_t>& audio_data);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> convertSoundToPlayable(
        const chunks::SoundChunk& sound);
    [[nodiscard]] static std::shared_ptr<chunks::SoundChunk> findSoundForMember(
        DirectorFile& dir_file,
        const std::shared_ptr<chunks::CastMemberChunk>& member);

private:
    struct PlayArgs {
        std::optional<lingo::Datum::CastMemberRef> member_ref;
        std::optional<int> loop_count;
    };

    [[nodiscard]] static PlayArgs extractPlayArgs(const lingo::Datum& args);
    struct Fade {
        int from_volume{0};
        int to_volume{0};
        std::int64_t start_ms{0};
        int duration_ms{0};
        bool stop_at_end{false};
    };

    void startFade(int channel_num,
                   int from_volume,
                   int to_volume,
                   int milliseconds,
                   bool stop_at_end);
    void finishFade(int channel_num);
    void playQueued(int channel_num);
    [[nodiscard]] std::int64_t nowMs() const;
    [[nodiscard]] int effectiveVolume(int channel_num) const;
    void applyVolume(int channel_num);
    void applyAllVolumes();

    AudioBackend* backend_{nullptr};
    bool enabled_{true};
    int sound_level_{7};
    bool sound_keep_device_{true};
    bool sound_mix_media_{true};
    std::array<int, MAX_CHANNELS + 1> volumes_{};
    std::array<std::optional<Fade>, MAX_CHANNELS + 1> fades_{};
    Clock clock_;
    std::array<int, MAX_CHANNELS + 1> loop_counts_{};
    std::array<int, MAX_CHANNELS + 1> pans_{};
    std::array<int, MAX_CHANNELS + 1> start_times_{};
    std::array<int, MAX_CHANNELS + 1> end_times_{};
    std::array<int, MAX_CHANNELS + 1> loop_start_times_{};
    std::array<int, MAX_CHANNELS + 1> loop_end_times_{};
    std::array<std::optional<lingo::Datum::CastMemberRef>, MAX_CHANNELS + 1> member_refs_{};
    std::array<SoundQueue, MAX_CHANNELS + 1> queues_{};
    std::unordered_map<int, DirectorFile*> cast_lib_files_;
    AudioResolver resolver_;
};

} // namespace libreshockwave::player::audio
