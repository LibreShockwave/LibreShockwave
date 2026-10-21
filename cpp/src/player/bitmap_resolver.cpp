#include "libreshockwave/player/bitmap_resolver.hpp"

#include <utility>

#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/frame/frame_context.hpp"

namespace libreshockwave::player {
namespace {

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {});
}

std::shared_ptr<const bitmap::Palette> moviePlatformPalette(const std::shared_ptr<moviesource::MovieSource>& file) {
    const auto config = file != nullptr ? file->config() : nullptr;
    const auto* palette = config != nullptr && config->platform == 2
        ? &bitmap::Palette::systemWinPalette()
        : &bitmap::Palette::systemMacPalette();
    return borrowedPalette(palette);
}

moviesource::MovieSource* mutableFileFor(const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!member || member->file() == nullptr) {
        return nullptr;
    }
    return const_cast<moviesource::MovieSource*>(member->file());
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

BitmapResolver::BitmapResolver(std::shared_ptr<moviesource::MovieSource> file,
                               cast::CastLibManager* cast_lib_manager,
                               frame::FrameContext* frame_context)
    : file_(std::move(file)),
      cast_lib_manager_(cast_lib_manager),
      frame_context_(frame_context) {}

const std::shared_ptr<moviesource::MovieSource>& BitmapResolver::file() const {
    return file_;
}

void BitmapResolver::setCastLibManager(cast::CastLibManager* cast_lib_manager) {
    cast_lib_manager_ = cast_lib_manager;
}

void BitmapResolver::setFrameContext(frame::FrameContext* frame_context) {
    frame_context_ = frame_context;
    movie_palette_frame_ = -1;
    movie_palette_.reset();
}

std::optional<bitmap::Bitmap> BitmapResolver::decodeBitmap(
    const std::shared_ptr<chunks::CastMemberChunk>& member) const {
    return decodeBitmap(member, nullptr);
}

std::optional<bitmap::Bitmap> BitmapResolver::decodeBitmap(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    const bitmap::Palette* palette_override) const {
    if (!member) {
        return std::nullopt;
    }

    std::shared_ptr<libreshockwave::cast::CastMember> runtime_member;
    std::shared_ptr<const bitmap::Palette> member_palette_override;
    const bitmap::Palette* effective_palette_override = palette_override;
    if (effective_palette_override == nullptr && cast_lib_manager_ != nullptr) {
        runtime_member = cast_lib_manager_->findRuntimeMember(member);
        if (runtime_member && runtime_member->runtimePaletteOverride()) {
            member_palette_override = runtime_member->runtimePaletteOverride();
            effective_palette_override = member_palette_override.get();
        }
    }
    auto member_asset = [&](moviesource::MovieSource* source) -> std::shared_ptr<moviesource::CastMemberAsset> {
        if (source == nullptr) {
            return nullptr;
        }
        return source->castMemberAssetForChunk(member);
    };
    auto with_palette_ref_metadata = [&](std::optional<bitmap::Bitmap> decoded) {
        if (decoded.has_value()) {
            decoded = decoded->copyWithDegenerateNativeAlphaOpaque();
            if (member_palette_override) {
                applyPaletteRefMetadata(*decoded, runtime_member);
            }
        }
        return decoded;
    };

    moviesource::MovieSource* member_file = mutableFileFor(member);
    if (member_file != nullptr && effective_palette_override == nullptr && file_ != nullptr && member_file != file_.get() &&
        member->isBitmap()) {
        if (auto cross_file_palette = resolvePaletteCrossFile(member, member_file)) {
            if (auto decoded = member_file->decodeBitmap(member_asset(member_file), cross_file_palette.get())) {
                return with_palette_ref_metadata(std::move(decoded));
            }
        }
    }

    if (member_file != nullptr) {
        if (auto decoded = member_file->decodeBitmap(member_asset(member_file), effective_palette_override)) {
            return with_palette_ref_metadata(std::move(decoded));
        }
    }

    if (file_ != nullptr && file_.get() != member_file) {
        if (auto decoded = file_->decodeBitmap(member_asset(file_.get()), effective_palette_override)) {
            return with_palette_ref_metadata(std::move(decoded));
        }
    }

    if (cast_lib_manager_ != nullptr) {
        for (const auto& [_, cast_lib] : cast_lib_manager_->castLibs()) {
            if (!cast_lib || !cast_lib->isLoaded()) {
                continue;
            }
            auto source = cast_lib->sourceFile();
            if (source != nullptr && source.get() != member_file && source != file_) {
                if (auto decoded = source->decodeBitmap(member_asset(source.get()), effective_palette_override)) {
                    return with_palette_ref_metadata(std::move(decoded));
                }
            }
        }
    }

    return std::nullopt;
}

std::shared_ptr<const bitmap::Bitmap> BitmapResolver::decodeBitmapForProvider(
    const chunks::CastMemberChunk& member,
    const bitmap::Palette* palette_override) const {
    auto decoded = decodeBitmap(borrowedMember(member), palette_override);
    if (!decoded.has_value()) {
        return nullptr;
    }
    return std::make_shared<bitmap::Bitmap>(std::move(*decoded));
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::getMoviePalette() const {
    const int frame = frame_context_ != nullptr ? frame_context_->currentFrame() - 1 : 0;
    if (frame != movie_palette_frame_) {
        movie_palette_frame_ = frame;
        movie_palette_ = resolveMoviePalette(frame);
    }
    return movie_palette_;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolvePaletteByMember(int cast_lib, int member_number) const {
    if (member_number < 0) {
        return borrowedPalette(&bitmap::Palette::builtIn(member_number));
    }

    if (cast_lib_manager_ != nullptr) {
        if (auto palette = cast_lib_manager_->resolvePaletteByMember(cast_lib > 0 ? cast_lib : 1, member_number)) {
            return palette;
        }
    }
    return file_ != nullptr ? file_->resolvePaletteByMemberNumber(member_number) : nullptr;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolveMoviePalette(int frame) const {
    if (file_ == nullptr) {
        return nullptr;
    }

    if (auto palette_data = file_->scorePalette(frame)) {
        if (palette_data->cast_member < 0) {
            return borrowedPalette(&bitmap::Palette::builtIn(palette_data->cast_member));
        }
        if (auto palette = resolvePaletteByMember(palette_data->cast_lib, palette_data->cast_member)) {
            return palette;
        }
    }

    auto config = file_->config();
    if (config != nullptr && config->default_palette_member != 0) {
        const int member = config->default_palette_member;
        if (member < 0) {
            return borrowedPalette(&bitmap::Palette::builtIn(member));
        }
        if (auto palette = resolvePaletteByMember(config->default_palette_cast_lib, member)) {
            return palette;
        }
    }

    return nullptr;
}

std::shared_ptr<const bitmap::Palette> BitmapResolver::resolvePaletteCrossFile(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    moviesource::MovieSource* member_file) const {
    if (!member || member_file == nullptr || member->specificData().size() < 10) {
        return nullptr;
    }

    const int director_version = member_file->config() != nullptr ? member_file->config()->director_version : 1200;
    const auto info = libreshockwave::cast::BitmapInfo::parse(member->specificData(), director_version);
    const auto exact_palette = member_file->resolvePaletteExact(info.palette_id);
    if (info.palette_id < 0) {
        // Built-in palette IDs are part of the bitmap's authored format and
        // remain valid when the bitmap is decoded through another movie.  In
        // particular, -102 is the Director 4 Windows system palette.  Do not
        // drop it here: doing so lets the source file's platform default
        // silently reinterpret indexed pixels with the Mac palette.
        if (bitmap::Palette::builtInSymbolName(info.palette_id).has_value()) {
            return exact_palette;
        }
        return nullptr;
    }

    if (exact_palette) {
        return nullptr;
    }

    if (info.palette_cast_lib > 0 && cast_lib_manager_ != nullptr) {
        if (auto palette = cast_lib_manager_->resolvePaletteByMember(info.palette_cast_lib, info.palette_id + 1)) {
            return palette;
        }
    }

    // A non-negative palette ID is local to the bitmap's cast library.  It is
    // not a global palette ID, so falling through to the movie or another
    // external cast can apply an unrelated CLUT.  This is especially visible
    // for old Windows casts whose default palette is the Windows system
    // palette: another cast's custom CLUT can turn black/navy pixels pink.
    return moviePlatformPalette(file_);
}

} // namespace libreshockwave::player
