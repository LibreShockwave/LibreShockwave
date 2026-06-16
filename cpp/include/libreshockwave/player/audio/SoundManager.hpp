#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/audio/AudioBackend.hpp"

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

    using AudioResolver = std::function<std::optional<std::vector<std::uint8_t>>(
        const lingo::Datum::CastMemberRef&)>;

    SoundManager();
    explicit SoundManager(DirectorFile* sourceFile);

    void setBackend(AudioBackend* backend);
    [[nodiscard]] AudioBackend* backend() const;

    void setAudioResolver(AudioResolver resolver);
    void setCastLibSourceFile(int castLib, DirectorFile* sourceFile);
    [[nodiscard]] DirectorFile* castLibSourceFile(int castLib) const;

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;
    void setSoundLevel(int level);
    [[nodiscard]] int getSoundLevel() const;
    void setSoundKeepDevice(bool keepDevice);
    [[nodiscard]] bool soundKeepDevice() const;
    void setSoundMixMedia(bool mixMedia);
    [[nodiscard]] bool soundMixMedia() const;
    void play(int channelNum, const lingo::Datum& args);
    void queue(int channelNum, const lingo::Datum& args);
    void playNext(int channelNum);
    void stop(int channelNum);
    void stopAll();
    void setVolume(int channelNum, int volume);
    [[nodiscard]] int getVolume(int channelNum) const;
    void setLoopCount(int channelNum, int loopCount);
    [[nodiscard]] int getLoopCount(int channelNum) const;
    void setPan(int channelNum, int pan);
    [[nodiscard]] int getPan(int channelNum) const;
    void setStartTime(int channelNum, int startTime);
    [[nodiscard]] int getStartTime(int channelNum) const;
    void setEndTime(int channelNum, int endTime);
    [[nodiscard]] int getEndTime(int channelNum) const;
    void setLoopStartTime(int channelNum, int loopStartTime);
    [[nodiscard]] int getLoopStartTime(int channelNum) const;
    void setLoopEndTime(int channelNum, int loopEndTime);
    [[nodiscard]] int getLoopEndTime(int channelNum) const;
    void setMember(int channelNum, const lingo::Datum::CastMemberRef& memberRef);
    [[nodiscard]] std::optional<lingo::Datum::CastMemberRef> getMember(int channelNum) const;
    void setPlaylist(int channelNum, const lingo::Datum& playlist);
    [[nodiscard]] std::vector<lingo::Datum> getPlaylist(int channelNum) const;
    [[nodiscard]] bool isPlaying(int channelNum) const;
    [[nodiscard]] int getElapsedTime(int channelNum) const;

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> resolveAudioData(const lingo::Datum& memberRef) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> resolveAudioData(
        const lingo::Datum::CastMemberRef& memberRef) const;

    [[nodiscard]] static bool isValidChannel(int channelNum);
    [[nodiscard]] static int clampVolume(int volume);
    [[nodiscard]] static int clampLoopCount(int loopCount);
    [[nodiscard]] static std::string_view detectFormat(const std::vector<std::uint8_t>& audioData);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> convertSoundToPlayable(
        const chunks::SoundChunk& sound);
    [[nodiscard]] static std::shared_ptr<chunks::SoundChunk> findSoundForMember(
        DirectorFile& dirFile,
        const std::shared_ptr<chunks::CastMemberChunk>& member);

private:
    struct PlayArgs {
        std::optional<lingo::Datum::CastMemberRef> memberRef;
        std::optional<int> loopCount;
    };

    [[nodiscard]] static PlayArgs extractPlayArgs(const lingo::Datum& args);
    [[nodiscard]] int effectiveVolume(int channelNum) const;
    void applyVolume(int channelNum);
    void applyAllVolumes();

    AudioBackend* backend_{nullptr};
    bool enabled_{true};
    int soundLevel_{7};
    bool soundKeepDevice_{true};
    bool soundMixMedia_{true};
    std::array<int, MAX_CHANNELS + 1> volumes_{};
    std::array<int, MAX_CHANNELS + 1> loopCounts_{};
    std::array<int, MAX_CHANNELS + 1> pans_{};
    std::array<int, MAX_CHANNELS + 1> startTimes_{};
    std::array<int, MAX_CHANNELS + 1> endTimes_{};
    std::array<int, MAX_CHANNELS + 1> loopStartTimes_{};
    std::array<int, MAX_CHANNELS + 1> loopEndTimes_{};
    std::array<std::optional<lingo::Datum::CastMemberRef>, MAX_CHANNELS + 1> memberRefs_{};
    std::array<std::vector<lingo::Datum>, MAX_CHANNELS + 1> playlists_{};
    std::unordered_map<int, DirectorFile*> castLibFiles_;
    AudioResolver resolver_;
};

} // namespace libreshockwave::player::audio
