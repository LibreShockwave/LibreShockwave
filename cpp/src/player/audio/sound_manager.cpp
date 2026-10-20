#include "libreshockwave/player/audio/sound_manager.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/audio/sound_converter.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/key_table_chunk.hpp"
#include "libreshockwave/chunks/media_chunk.hpp"
#include "libreshockwave/chunks/sound_chunk.hpp"

namespace libreshockwave::player::audio {

namespace {

lingo::Datum getProp(const lingo::Datum::PropList& props, std::string_view name) {
    auto value = props.get(lingo::Datum::symbol(std::string(name)));
    if (!value.isVoid()) {
        return value;
    }
    return props.get(lingo::Datum::of(std::string(name)));
}

} // namespace

SoundManager::SoundManager() {
    volumes_.fill(255);
    volumes_[0] = 0;
    loop_counts_.fill(1);
    loop_counts_[0] = 0;
}

SoundManager::SoundManager(DirectorFile* source_file) : SoundManager() {
    setCastLibSourceFile(1, source_file);
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

void SoundManager::setCastLibSourceFile(int cast_lib, DirectorFile* source_file) {
    if (cast_lib <= 0) {
        return;
    }
    if (source_file == nullptr) {
        cast_lib_files_.erase(cast_lib);
        return;
    }
    cast_lib_files_[cast_lib] = source_file;
}

DirectorFile* SoundManager::castLibSourceFile(int cast_lib) const {
    const auto found = cast_lib_files_.find(cast_lib);
    return found == cast_lib_files_.end() ? nullptr : found->second;
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
    if (sound_level_ == clamped) {
        return;
    }
    sound_level_ = clamped;
    applyAllVolumes();
}

int SoundManager::getSoundLevel() const {
    return sound_level_;
}

void SoundManager::setSoundKeepDevice(bool keep_device) {
    sound_keep_device_ = keep_device;
}

bool SoundManager::soundKeepDevice() const {
    return sound_keep_device_;
}

void SoundManager::setSoundMixMedia(bool mix_media) {
    sound_mix_media_ = mix_media;
}

bool SoundManager::soundMixMedia() const {
    return sound_mix_media_;
}

void SoundManager::play(int channel_num, const lingo::Datum& args) {
    if (!enabled_ || backend_ == nullptr || !isValidChannel(channel_num)) {
        return;
    }

    const auto parsed = extractPlayArgs(args);
    if (!parsed.member_ref.has_value()) {
        return;
    }

    const auto audio_data = resolveAudioData(*parsed.member_ref);
    if (!audio_data.has_value() || audio_data->empty()) {
        return;
    }

    fades_[static_cast<std::size_t>(channel_num)].reset();
    setMember(channel_num, *parsed.member_ref);
    const int loop_count = parsed.loop_count.value_or(getLoopCount(channel_num));
    backend_->play(channel_num, *audio_data, detectFormat(*audio_data), loop_count);
    applyVolume(channel_num);
}

void SoundManager::play(int channel_num) {
    if (!enabled_ || backend_ == nullptr || !isValidChannel(channel_num)) {
        return;
    }
    queues_[static_cast<std::size_t>(channel_num)].start();
    if (!backend_->isPlaying(channel_num)) {
        playQueued(channel_num);
    }
}

void SoundManager::queue(int channel_num, const lingo::Datum& args) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    queues_[static_cast<std::size_t>(channel_num)].push(args);
}

void SoundManager::playNext(int channel_num) {
    if (!enabled_ || backend_ == nullptr || !isValidChannel(channel_num)) {
        return;
    }
    queues_[static_cast<std::size_t>(channel_num)].start();
    playQueued(channel_num);
}

void SoundManager::updateQueues() {
    if (!enabled_ || backend_ == nullptr) {
        return;
    }
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        const auto& queue = queues_[static_cast<std::size_t>(channel)];
        if (queue.isRunning() && !backend_->isPlaying(channel)) {
            playQueued(channel);
        }
    }
}

void SoundManager::stop(int channel_num) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    fades_[static_cast<std::size_t>(channel_num)].reset();
    queues_[static_cast<std::size_t>(channel_num)].clear();
    if (backend_ != nullptr) {
        backend_->stop(channel_num);
    }
}

void SoundManager::stopAll() {
    fades_.fill(std::nullopt);
    for (auto& queue : queues_) {
        queue.clear();
    }
    if (backend_ != nullptr) {
        backend_->stopAll();
    }
}

void SoundManager::setVolume(int channel_num, int volume) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    fades_[static_cast<std::size_t>(channel_num)].reset();
    const int clamped = clampVolume(volume);
    volumes_[static_cast<std::size_t>(channel_num)] = clamped;
    applyVolume(channel_num);
}

void SoundManager::setClock(Clock clock) {
    clock_ = std::move(clock);
}

void SoundManager::fadeIn(int channel_num, int milliseconds) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    // Director drops the channel to silence at once and brings it back up.
    const int target = volumes_[static_cast<std::size_t>(channel_num)];
    startFade(channel_num, 0, target, milliseconds, false);
}

void SoundManager::fadeOut(int channel_num, int milliseconds) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    startFade(channel_num, volumes_[static_cast<std::size_t>(channel_num)], 0, milliseconds, true);
}

void SoundManager::fadeTo(int channel_num, int volume, int milliseconds) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    const int from = volumes_[static_cast<std::size_t>(channel_num)];
    startFade(channel_num, from, clampVolume(volume), milliseconds, false);
}

void SoundManager::updateFades() {
    const std::int64_t now = nowMs();
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        const auto& fade = fades_[static_cast<std::size_t>(channel)];
        if (!fade.has_value()) {
            continue;
        }
        const std::int64_t elapsed = std::max<std::int64_t>(0, now - fade->start_ms);
        if (elapsed >= fade->duration_ms) {
            finishFade(channel);
            continue;
        }
        const std::int64_t span = fade->to_volume - fade->from_volume;
        volumes_[static_cast<std::size_t>(channel)] =
            fade->from_volume + static_cast<int>(span * elapsed / fade->duration_ms);
        applyVolume(channel);
    }
}

bool SoundManager::isFading(int channel_num) const {
    return isValidChannel(channel_num) && fades_[static_cast<std::size_t>(channel_num)].has_value();
}

int SoundManager::getVolume(int channel_num) const {
    if (!isValidChannel(channel_num)) {
        return 255;
    }
    return volumes_[static_cast<std::size_t>(channel_num)];
}

void SoundManager::setLoopCount(int channel_num, int loop_count) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    loop_counts_[static_cast<std::size_t>(channel_num)] = clampLoopCount(loop_count);
}

int SoundManager::getLoopCount(int channel_num) const {
    if (!isValidChannel(channel_num)) {
        return 1;
    }
    return loop_counts_[static_cast<std::size_t>(channel_num)];
}

void SoundManager::setPan(int channel_num, int pan) {
    if (isValidChannel(channel_num)) {
        pans_[static_cast<std::size_t>(channel_num)] = pan;
    }
}

int SoundManager::getPan(int channel_num) const {
    return isValidChannel(channel_num) ? pans_[static_cast<std::size_t>(channel_num)] : 0;
}

void SoundManager::setStartTime(int channel_num, int start_time) {
    if (isValidChannel(channel_num)) {
        start_times_[static_cast<std::size_t>(channel_num)] = start_time;
    }
}

int SoundManager::getStartTime(int channel_num) const {
    return isValidChannel(channel_num) ? start_times_[static_cast<std::size_t>(channel_num)] : 0;
}

void SoundManager::setEndTime(int channel_num, int end_time) {
    if (isValidChannel(channel_num)) {
        end_times_[static_cast<std::size_t>(channel_num)] = end_time;
    }
}

int SoundManager::getEndTime(int channel_num) const {
    return isValidChannel(channel_num) ? end_times_[static_cast<std::size_t>(channel_num)] : 0;
}

void SoundManager::setLoopStartTime(int channel_num, int loop_start_time) {
    if (isValidChannel(channel_num)) {
        loop_start_times_[static_cast<std::size_t>(channel_num)] = loop_start_time;
    }
}

int SoundManager::getLoopStartTime(int channel_num) const {
    return isValidChannel(channel_num) ? loop_start_times_[static_cast<std::size_t>(channel_num)] : 0;
}

void SoundManager::setLoopEndTime(int channel_num, int loop_end_time) {
    if (isValidChannel(channel_num)) {
        loop_end_times_[static_cast<std::size_t>(channel_num)] = loop_end_time;
    }
}

int SoundManager::getLoopEndTime(int channel_num) const {
    return isValidChannel(channel_num) ? loop_end_times_[static_cast<std::size_t>(channel_num)] : 0;
}

void SoundManager::setMember(int channel_num, const lingo::Datum::CastMemberRef& member_ref) {
    if (isValidChannel(channel_num)) {
        member_refs_[static_cast<std::size_t>(channel_num)] = member_ref;
    }
}

std::optional<lingo::Datum::CastMemberRef> SoundManager::getMember(int channel_num) const {
    return isValidChannel(channel_num) ? member_refs_[static_cast<std::size_t>(channel_num)] : std::nullopt;
}

void SoundManager::setPlaylist(int channel_num, const lingo::Datum& playlist) {
    if (isValidChannel(channel_num)) {
        queues_[static_cast<std::size_t>(channel_num)].replace(playlist);
    }
}

std::vector<lingo::Datum> SoundManager::getPlaylist(int channel_num) const {
    if (!isValidChannel(channel_num)) {
        return {};
    }
    return queues_[static_cast<std::size_t>(channel_num)].entries();
}

bool SoundManager::isPlaying(int channel_num) const {
    return enabled_ && backend_ != nullptr && isValidChannel(channel_num) && backend_->isPlaying(channel_num);
}

bool SoundManager::isBusy(int channel_num) const {
    if (isPlaying(channel_num)) {
        return true;
    }
    return isValidChannel(channel_num) && queues_[static_cast<std::size_t>(channel_num)].hasWaiting();
}

int SoundManager::getElapsedTime(int channel_num) const {
    return enabled_ && backend_ != nullptr && isValidChannel(channel_num) ? backend_->getElapsedTime(channel_num) : 0;
}

std::optional<std::vector<std::uint8_t>> SoundManager::resolveAudioData(const lingo::Datum& member_ref) const {
    const auto* cast_member_ref = member_ref.asCastMemberRef();
    if (cast_member_ref == nullptr) {
        return std::nullopt;
    }
    return resolveAudioData(*cast_member_ref);
}

std::optional<std::vector<std::uint8_t>> SoundManager::resolveAudioData(
    const lingo::Datum::CastMemberRef& member_ref) const {
    if (resolver_) {
        return resolver_(member_ref);
    }

    auto* source_file = castLibSourceFile(member_ref.cast_lib);
    if (source_file == nullptr) {
        return std::nullopt;
    }

    auto member = source_file->getCastMemberByNumber(member_ref.cast_lib, member_ref.memberNum());
    if (!member) {
        return std::nullopt;
    }

    auto sound = findSoundForMember(*source_file, member);
    return sound ? convertSoundToPlayable(*sound) : std::nullopt;
}

bool SoundManager::isValidChannel(int channel_num) {
    return channel_num >= 1 && channel_num <= MAX_CHANNELS;
}

int SoundManager::clampVolume(int volume) {
    return std::clamp(volume, 0, 255);
}

int SoundManager::clampLoopCount(int loop_count) {
    return std::max(0, loop_count);
}

std::string_view SoundManager::detectFormat(const std::vector<std::uint8_t>& audio_data) {
    if (audio_data.size() > 2 && audio_data[0] == 'R' && audio_data[1] == 'I') {
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
    DirectorFile& dir_file,
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!member || !dir_file.keyTable()) {
        return nullptr;
    }

    for (const auto& chunk : dir_file.getLinkedChunksForMember(member)) {
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

    if (const auto* cast_member = args.asCastMemberRef()) {
        result.member_ref = *cast_member;
        return result;
    }

    if (args.isPropList()) {
        const auto& props = args.propListValue();
        const auto member = getProp(props, "member");
        if (const auto* cast_member = member.asCastMemberRef()) {
            result.member_ref = *cast_member;
        }
        const auto loop_count = getProp(props, "loopCount");
        if (!loop_count.isVoid()) {
            result.loop_count = clampLoopCount(loop_count.intValue());
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

void SoundManager::startFade(int channel_num,
                             int from_volume,
                             int to_volume,
                             int milliseconds,
                             bool stop_at_end) {
    fades_[static_cast<std::size_t>(channel_num)] =
        Fade{from_volume, to_volume, nowMs(), milliseconds, stop_at_end};
    volumes_[static_cast<std::size_t>(channel_num)] = from_volume;
    applyVolume(channel_num);
    if (milliseconds <= 0) {
        finishFade(channel_num);
    }
}

void SoundManager::finishFade(int channel_num) {
    auto& slot = fades_[static_cast<std::size_t>(channel_num)];
    const Fade fade = *slot;
    slot.reset();
    volumes_[static_cast<std::size_t>(channel_num)] = fade.to_volume;
    applyVolume(channel_num);
    if (fade.stop_at_end) {
        stop(channel_num);
    }
}

// Entries that do not resolve to audio are dropped so the next one gets its turn.
void SoundManager::playQueued(int channel_num) {
    auto& queue = queues_[static_cast<std::size_t>(channel_num)];
    do {
        auto next = queue.popFront();
        if (!next.has_value()) {
            queue.clear();
            return;
        }
        play(channel_num, *next);
    } while (!backend_->isPlaying(channel_num));
}

std::int64_t SoundManager::nowMs() const {
    if (clock_) {
        return clock_();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int SoundManager::effectiveVolume(int channel_num) const {
    if (!isValidChannel(channel_num)) {
        return 255;
    }
    const int channel_volume = volumes_[static_cast<std::size_t>(channel_num)];
    return (channel_volume * sound_level_) / 7;
}

void SoundManager::applyVolume(int channel_num) {
    if (backend_ != nullptr && isValidChannel(channel_num)) {
        backend_->setVolume(channel_num, effectiveVolume(channel_num));
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
