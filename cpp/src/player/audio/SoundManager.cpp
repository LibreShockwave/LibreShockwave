#include "libreshockwave/player/audio/SoundManager.hpp"

#include <algorithm>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/audio/SoundConverter.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/KeyTableChunk.hpp"
#include "libreshockwave/chunks/MediaChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"

namespace libreshockwave::player::audio {

namespace {

lingo::Datum getProp(const lingo::Datum::PropList& props, std::string_view name) {
    auto value = props.get(lingo::Datum::symbol(std::string(name)));
    if (!value.isVoid()) {
        return value;
    }
    return props.get(lingo::Datum::of(std::string(name)));
}

lingo::Datum normalizePlaylistEntry(const lingo::Datum& entry) {
    if (entry.isList()) {
        const auto& items = entry.listValue().items();
        if (items.size() == 1 && items.front().isPropList()) {
            return items.front().deepCopy();
        }
    }
    return entry.deepCopy();
}

} // namespace

SoundManager::SoundManager() {
    volumes_.fill(255);
    volumes_[0] = 0;
    loopCounts_.fill(1);
    loopCounts_[0] = 0;
}

SoundManager::SoundManager(DirectorFile* sourceFile) : SoundManager() {
    setCastLibSourceFile(1, sourceFile);
}

void SoundManager::setBackend(AudioBackend* backend) {
    backend_ = backend;
}

AudioBackend* SoundManager::backend() const {
    return backend_;
}

void SoundManager::setAudioResolver(AudioResolver resolver) {
    resolver_ = std::move(resolver);
}

void SoundManager::setCastLibSourceFile(int castLib, DirectorFile* sourceFile) {
    if (castLib <= 0) {
        return;
    }
    if (sourceFile == nullptr) {
        castLibFiles_.erase(castLib);
        return;
    }
    castLibFiles_[castLib] = sourceFile;
}

DirectorFile* SoundManager::castLibSourceFile(int castLib) const {
    const auto found = castLibFiles_.find(castLib);
    return found == castLibFiles_.end() ? nullptr : found->second;
}

void SoundManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_) {
        stopAll();
    }
}

bool SoundManager::isEnabled() const {
    return enabled_;
}

void SoundManager::setSoundLevel(int level) {
    const int clamped = std::clamp(level, 0, 7);
    if (soundLevel_ == clamped) {
        return;
    }
    soundLevel_ = clamped;
    applyAllVolumes();
}

int SoundManager::getSoundLevel() const {
    return soundLevel_;
}

void SoundManager::setSoundKeepDevice(bool keepDevice) {
    soundKeepDevice_ = keepDevice;
}

bool SoundManager::soundKeepDevice() const {
    return soundKeepDevice_;
}

void SoundManager::setSoundMixMedia(bool mixMedia) {
    soundMixMedia_ = mixMedia;
}

bool SoundManager::soundMixMedia() const {
    return soundMixMedia_;
}

void SoundManager::play(int channelNum, const lingo::Datum& args) {
    if (!enabled_ || backend_ == nullptr || !isValidChannel(channelNum)) {
        return;
    }

    const auto parsed = extractPlayArgs(args);
    if (!parsed.memberRef.has_value()) {
        return;
    }

    const auto audioData = resolveAudioData(*parsed.memberRef);
    if (!audioData.has_value() || audioData->empty()) {
        return;
    }

    setMember(channelNum, *parsed.memberRef);
    const int loopCount = parsed.loopCount.value_or(getLoopCount(channelNum));
    backend_->play(channelNum, *audioData, detectFormat(*audioData), loopCount);
    applyVolume(channelNum);
}

void SoundManager::queue(int channelNum, const lingo::Datum& args) {
    if (!isValidChannel(channelNum)) {
        return;
    }
    playlists_[static_cast<std::size_t>(channelNum)].push_back(normalizePlaylistEntry(args));
}

void SoundManager::playNext(int channelNum) {
    if (!enabled_ || backend_ == nullptr || !isValidChannel(channelNum)) {
        return;
    }

    auto& playlist = playlists_[static_cast<std::size_t>(channelNum)];
    if (playlist.empty()) {
        return;
    }

    const auto next = playlist.front().deepCopy();
    playlist.erase(playlist.begin());
    play(channelNum, next);
}

void SoundManager::stop(int channelNum) {
    if (backend_ == nullptr || !isValidChannel(channelNum)) {
        return;
    }
    backend_->stop(channelNum);
}

void SoundManager::stopAll() {
    if (backend_ != nullptr) {
        backend_->stopAll();
    }
}

void SoundManager::setVolume(int channelNum, int volume) {
    if (!isValidChannel(channelNum)) {
        return;
    }
    const int clamped = clampVolume(volume);
    volumes_[static_cast<std::size_t>(channelNum)] = clamped;
    applyVolume(channelNum);
}

int SoundManager::getVolume(int channelNum) const {
    if (!isValidChannel(channelNum)) {
        return 255;
    }
    return volumes_[static_cast<std::size_t>(channelNum)];
}

void SoundManager::setLoopCount(int channelNum, int loopCount) {
    if (!isValidChannel(channelNum)) {
        return;
    }
    loopCounts_[static_cast<std::size_t>(channelNum)] = clampLoopCount(loopCount);
}

int SoundManager::getLoopCount(int channelNum) const {
    if (!isValidChannel(channelNum)) {
        return 1;
    }
    return loopCounts_[static_cast<std::size_t>(channelNum)];
}

void SoundManager::setPan(int channelNum, int pan) {
    if (isValidChannel(channelNum)) {
        pans_[static_cast<std::size_t>(channelNum)] = pan;
    }
}

int SoundManager::getPan(int channelNum) const {
    return isValidChannel(channelNum) ? pans_[static_cast<std::size_t>(channelNum)] : 0;
}

void SoundManager::setStartTime(int channelNum, int startTime) {
    if (isValidChannel(channelNum)) {
        startTimes_[static_cast<std::size_t>(channelNum)] = startTime;
    }
}

int SoundManager::getStartTime(int channelNum) const {
    return isValidChannel(channelNum) ? startTimes_[static_cast<std::size_t>(channelNum)] : 0;
}

void SoundManager::setEndTime(int channelNum, int endTime) {
    if (isValidChannel(channelNum)) {
        endTimes_[static_cast<std::size_t>(channelNum)] = endTime;
    }
}

int SoundManager::getEndTime(int channelNum) const {
    return isValidChannel(channelNum) ? endTimes_[static_cast<std::size_t>(channelNum)] : 0;
}

void SoundManager::setLoopStartTime(int channelNum, int loopStartTime) {
    if (isValidChannel(channelNum)) {
        loopStartTimes_[static_cast<std::size_t>(channelNum)] = loopStartTime;
    }
}

int SoundManager::getLoopStartTime(int channelNum) const {
    return isValidChannel(channelNum) ? loopStartTimes_[static_cast<std::size_t>(channelNum)] : 0;
}

void SoundManager::setLoopEndTime(int channelNum, int loopEndTime) {
    if (isValidChannel(channelNum)) {
        loopEndTimes_[static_cast<std::size_t>(channelNum)] = loopEndTime;
    }
}

int SoundManager::getLoopEndTime(int channelNum) const {
    return isValidChannel(channelNum) ? loopEndTimes_[static_cast<std::size_t>(channelNum)] : 0;
}

void SoundManager::setMember(int channelNum, const lingo::Datum::CastMemberRef& memberRef) {
    if (isValidChannel(channelNum)) {
        memberRefs_[static_cast<std::size_t>(channelNum)] = memberRef;
    }
}

std::optional<lingo::Datum::CastMemberRef> SoundManager::getMember(int channelNum) const {
    return isValidChannel(channelNum) ? memberRefs_[static_cast<std::size_t>(channelNum)] : std::nullopt;
}

void SoundManager::setPlaylist(int channelNum, const lingo::Datum& playlist) {
    if (!isValidChannel(channelNum)) {
        return;
    }

    auto& entries = playlists_[static_cast<std::size_t>(channelNum)];
    entries.clear();
    if (playlist.isVoid()) {
        return;
    }
    if (playlist.isList()) {
        for (const auto& item : playlist.listValue().items()) {
            entries.push_back(normalizePlaylistEntry(item));
        }
        return;
    }
    entries.push_back(normalizePlaylistEntry(playlist));
}

std::vector<lingo::Datum> SoundManager::getPlaylist(int channelNum) const {
    if (!isValidChannel(channelNum)) {
        return {};
    }

    std::vector<lingo::Datum> result;
    const auto& entries = playlists_[static_cast<std::size_t>(channelNum)];
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.deepCopy());
    }
    return result;
}

bool SoundManager::isPlaying(int channelNum) const {
    return enabled_ && backend_ != nullptr && isValidChannel(channelNum) && backend_->isPlaying(channelNum);
}

int SoundManager::getElapsedTime(int channelNum) const {
    return enabled_ && backend_ != nullptr && isValidChannel(channelNum) ? backend_->getElapsedTime(channelNum) : 0;
}

std::optional<std::vector<std::uint8_t>> SoundManager::resolveAudioData(const lingo::Datum& memberRef) const {
    const auto* castMemberRef = memberRef.asCastMemberRef();
    if (castMemberRef == nullptr) {
        return std::nullopt;
    }
    return resolveAudioData(*castMemberRef);
}

std::optional<std::vector<std::uint8_t>> SoundManager::resolveAudioData(
    const lingo::Datum::CastMemberRef& memberRef) const {
    if (resolver_) {
        return resolver_(memberRef);
    }

    auto* sourceFile = castLibSourceFile(memberRef.castLib);
    if (sourceFile == nullptr) {
        return std::nullopt;
    }

    auto member = sourceFile->getCastMemberByNumber(memberRef.castLib, memberRef.memberNum());
    if (!member) {
        return std::nullopt;
    }

    auto sound = findSoundForMember(*sourceFile, member);
    return sound ? convertSoundToPlayable(*sound) : std::nullopt;
}

bool SoundManager::isValidChannel(int channelNum) {
    return channelNum >= 1 && channelNum <= MAX_CHANNELS;
}

int SoundManager::clampVolume(int volume) {
    return std::clamp(volume, 0, 255);
}

int SoundManager::clampLoopCount(int loopCount) {
    return std::max(0, loopCount);
}

std::string_view SoundManager::detectFormat(const std::vector<std::uint8_t>& audioData) {
    if (audioData.size() > 2 && audioData[0] == 'R' && audioData[1] == 'I') {
        return "wav";
    }
    return "mp3";
}

std::optional<std::vector<std::uint8_t>> SoundManager::convertSoundToPlayable(const chunks::SoundChunk& sound) {
    if (sound.isMp3()) {
        auto mp3 = libreshockwave::audio::SoundConverter::extractMp3(sound);
        if (mp3.has_value() && !mp3->empty()) {
            return mp3;
        }
        return std::nullopt;
    }

    if (sound.isAdpcm()) {
        return libreshockwave::audio::SoundConverter::imaAdpcmToWav(
            sound.audioData(),
            sound.sampleRate(),
            sound.channelCount(),
            0,
            0);
    }

    return libreshockwave::audio::SoundConverter::toWav(sound);
}

std::shared_ptr<chunks::SoundChunk> SoundManager::findSoundForMember(
    DirectorFile& dirFile,
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!member || !dirFile.keyTable()) {
        return nullptr;
    }

    for (const auto& chunk : dirFile.getLinkedChunksForMember(member)) {
        if (auto sound = std::dynamic_pointer_cast<chunks::SoundChunk>(chunk)) {
            return sound;
        }
        if (auto media = std::dynamic_pointer_cast<chunks::MediaChunk>(chunk)) {
            return std::make_shared<chunks::SoundChunk>(media->toSoundChunk());
        }
    }
    return nullptr;
}

SoundManager::PlayArgs SoundManager::extractPlayArgs(const lingo::Datum& args) {
    PlayArgs result;

    if (const auto* castMember = args.asCastMemberRef()) {
        result.memberRef = *castMember;
        return result;
    }

    if (args.isPropList()) {
        const auto& props = args.propListValue();
        const auto member = getProp(props, "member");
        if (const auto* castMember = member.asCastMemberRef()) {
            result.memberRef = *castMember;
        }
        const auto loopCount = getProp(props, "loopCount");
        if (!loopCount.isVoid()) {
            result.loopCount = clampLoopCount(loopCount.intValue());
        }
        return result;
    }

    if (args.isList()) {
        const auto& items = args.listValue().items();
        if (!items.empty() && items.front().isPropList()) {
            return extractPlayArgs(items.front());
        }
    }

    return result;
}

int SoundManager::effectiveVolume(int channelNum) const {
    if (!isValidChannel(channelNum)) {
        return 255;
    }
    const int channelVolume = volumes_[static_cast<std::size_t>(channelNum)];
    return (channelVolume * soundLevel_) / 7;
}

void SoundManager::applyVolume(int channelNum) {
    if (backend_ != nullptr && isValidChannel(channelNum)) {
        backend_->setVolume(channelNum, effectiveVolume(channelNum));
    }
}

void SoundManager::applyAllVolumes() {
    if (backend_ == nullptr) {
        return;
    }
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        if (backend_->isPlaying(channel)) {
            applyVolume(channel);
        }
    }
}

} // namespace libreshockwave::player::audio
