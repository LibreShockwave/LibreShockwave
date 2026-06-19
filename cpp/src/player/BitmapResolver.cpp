#include "libreshockwave/player/BitmapResolver.hpp"

#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/frame/FrameContext.hpp"

namespace libreshockwave::player {
namespace {

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {});
}

DirectorFile* mutableFileFor(const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!member || member->file() == nullptr) {
        return nullptr;
    }
    return const_cast<DirectorFile*>(member->file());
}

std::shared_ptr<chunks::CastMemberChunk> borrowedMember(const chunks::CastMemberChunk& member) {
    return std::shared_ptr<chunks::CastMemberChunk>(
        const_cast<chunks::CastMemberChunk*>(&member),
        [](chunks::CastMemberChunk*) {});
}

void applyPaletteRefMetadata(bitmap::Bitmap& bitmap,
                             const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (!member) {
        return;
    }
    if (member->paletteRefCastLib() >= 1 && member->paletteRefMemberNum() >= 1) {
        bitmap.setPaletteRefCastMember(member->paletteRefCastLib(), member->paletteRefMemberNum());
    } else if (member->paletteRefSystemName().has_value()) {
        bitmap.setPaletteRefSystemName(*member->paletteRefSystemName());
    }
}

} // namespace

BitmapResolver::BitmapResolver(std::shared_ptr<DirectorFile> file,
                               cast::CastLibManager* castLibManager,
                               frame::FrameContext* frameContext)
    : file_(std::move(file)),
      castLibManager_(castLibManager),
      frameContext_(frameContext) {}

const std::shared_ptr<DirectorFile>& BitmapResolver::file() const {
    return file_;
}

void BitmapResolver::setCastLibManager(cast::CastLibManager* castLibManager) {
    castLibManager_ = castLibManager;
}

void BitmapResolver::setFrameContext(frame::FrameContext* frameContext) {
    frameContext_ = frameContext;
    moviePaletteFrame_ = -1;
    moviePalette_.reset();
}

std::optional<bitmap::Bitmap> BitmapResolver::decodeBitmap(
    const std::shared_ptr<chunks::CastMemberChunk>& member) const {
    return decodeBitmap(member, nullptr);
}

std::optional<bitmap::Bitmap> BitmapResolver::decodeBitmap(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    const bitmap::Palette* paletteOverride) const {
    if (!member) {
        return std::nullopt;
    }

    std::shared_ptr<libreshockwave::cast::CastMember> runtimeMember;
    std::shared_ptr<const bitmap::Palette> memberPaletteOverride;
    const bitmap::Palette* effectivePaletteOverride = paletteOverride;
    if (effectivePaletteOverride == nullptr && castLibManager_ != nullptr) {
        runtimeMember = castLibManager_->findRuntimeMember(member);
        if (runtimeMember && runtimeMember->runtimePaletteOverride()) {
            memberPaletteOverride = runtimeMember->runtimePaletteOverride();
            effectivePaletteOverride = memberPaletteOverride.get();
        }
    }
    auto withPaletteRefMetadata = [&](std::optional<bitmap::Bitmap> decoded) {
        if (decoded.has_value()) {
            decoded = decoded->copyWithDegenerateNativeAlphaOpaque();
            if (memberPaletteOverride) {
                applyPaletteRefMetadata(*decoded, runtimeMember);
            }
        }
        return decoded;
    };

    DirectorFile* memberFile = mutableFileFor(member);
    if (memberFile != nullptr && effectivePaletteOverride == nullptr && file_ != nullptr && memberFile != file_.get() &&
        member->isBitmap()) {
        if (auto crossFilePalette = resolvePaletteCrossFile(member, memberFile)) {
            if (auto decoded = memberFile->decodeBitmap(member, crossFilePalette.get())) {
                return withPaletteRefMetadata(std::move(decoded));
            }
        }
    }

    if (memberFile != nullptr) {
        if (auto decoded = memberFile->decodeBitmap(member, effectivePaletteOverride)) {
            return withPaletteRefMetadata(std::move(decoded));
        }
    }

    if (file_ != nullptr && file_.get() != memberFile) {
        if (auto decoded = file_->decodeBitmap(member, effectivePaletteOverride)) {
            return withPaletteRefMetadata(std::move(decoded));
        }
    }

    if (castLibManager_ != nullptr) {
        for (const auto& [_, castLib] : castLibManager_->castLibs()) {
            if (!castLib || !castLib->isLoaded()) {
                continue;
            }
            auto source = castLib->sourceFile();
            if (source != nullptr && source.get() != memberFile && source != file_) {
                if (auto decoded = source->decodeBitmap(member, effectivePaletteOverride)) {
                    return withPaletteRefMetadata(std::move(decoded));
                }
            }
        }
    }

    return std::nullopt;
}

std::shared_ptr<const bitmap::Bitmap> BitmapResolver::decodeBitmapForProvider(
    const chunks::CastMemberChunk& member,
    const bitmap::Palette* paletteOverride) const {
    auto decoded = decodeBitmap(borrowedMember(member), paletteOverride);
    if (!decoded.has_value()) {
        return nullptr;
    }
    return std::make_shared<bitmap::Bitmap>(std::move(*decoded));
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::getMoviePalette() const {
    const int frame = frameContext_ != nullptr ? frameContext_->currentFrame() - 1 : 0;
    if (frame != moviePaletteFrame_) {
        moviePaletteFrame_ = frame;
        moviePalette_ = resolveMoviePalette(frame);
    }
    return moviePalette_;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolvePaletteByMember(int castLib, int memberNumber) const {
    if (memberNumber < 0) {
        return borrowedPalette(&bitmap::Palette::builtIn(memberNumber));
    }

    if (castLibManager_ != nullptr) {
        if (auto palette = castLibManager_->resolvePaletteByMember(castLib > 0 ? castLib : 1, memberNumber)) {
            return palette;
        }
    }
    return file_ != nullptr ? file_->resolvePaletteByMemberNumber(memberNumber) : nullptr;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolveMoviePalette(int frame) const {
    if (file_ == nullptr) {
        return nullptr;
    }

    if (auto paletteData = file_->getScorePalette(frame)) {
        if (paletteData->castMember < 0) {
            return borrowedPalette(&bitmap::Palette::builtIn(paletteData->castMember));
        }
        if (auto palette = resolvePaletteByMember(paletteData->castLib, paletteData->castMember)) {
            return palette;
        }
    }

    auto config = file_->config();
    if (config != nullptr && config->defaultPaletteMember() != 0) {
        const int member = config->defaultPaletteMember();
        if (member < 0) {
            return borrowedPalette(&bitmap::Palette::builtIn(member));
        }
        if (auto palette = resolvePaletteByMember(config->defaultPaletteCastLib(), member)) {
            return palette;
        }
    }

    return nullptr;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolvePaletteCrossFile(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    DirectorFile* memberFile) const {
    if (!member || memberFile == nullptr || member->specificData().size() < 10) {
        return nullptr;
    }

    const int directorVersion = memberFile->config() != nullptr ? memberFile->config()->directorVersion() : 1200;
    const auto info = libreshockwave::cast::BitmapInfo::parse(member->specificData(), directorVersion);
    if (info.paletteId < 0) {
        return nullptr;
    }

    if (memberFile->resolvePaletteExact(info.paletteId)) {
        return nullptr;
    }

    if (info.paletteCastLib > 0 && castLibManager_ != nullptr) {
        if (auto palette = castLibManager_->resolvePaletteByMember(info.paletteCastLib, info.paletteId + 1)) {
            return palette;
        }
    }

    if (file_ != nullptr) {
        if (auto palette = file_->resolvePaletteExact(info.paletteId)) {
            return palette;
        }
    }

    if (castLibManager_ != nullptr) {
        for (const auto& [_, castLib] : castLibManager_->castLibs()) {
            if (!castLib || !castLib->isLoaded()) {
                continue;
            }
            auto source = castLib->sourceFile();
            if (source != nullptr && source.get() != memberFile && source != file_) {
                if (auto palette = source->resolvePaletteExact(info.paletteId)) {
                    return palette;
                }
            }
        }
    }

    return nullptr;
}

} // namespace libreshockwave::player
