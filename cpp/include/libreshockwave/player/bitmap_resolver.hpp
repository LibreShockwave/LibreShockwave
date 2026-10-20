#pragma once

#include <memory>
#include <optional>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class CastMemberChunk;
}

namespace libreshockwave::player::cast {
class CastLibManager;
}

namespace libreshockwave::player::frame {
class FrameContext;
}

namespace libreshockwave::player {

class BitmapResolver {
public:
    BitmapResolver(std::shared_ptr<DirectorFile> file = nullptr,
                   cast::CastLibManager* cast_lib_manager = nullptr,
                   frame::FrameContext* frame_context = nullptr);

    [[nodiscard]] const std::shared_ptr<DirectorFile>& file() const;
    void setCastLibManager(cast::CastLibManager* cast_lib_manager);
    void setFrameContext(frame::FrameContext* frame_context);

    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<chunks::CastMemberChunk>& member) const;
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        const bitmap::Palette* palette_override) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> decodeBitmapForProvider(
        const chunks::CastMemberChunk& member,
        const bitmap::Palette* palette_override) const;

    [[nodiscard]] std::shared_ptr<const bitmap::Palette> getMoviePalette() const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMember(int cast_lib, int member_number) const;

private:
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveMoviePalette(int frame) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteCrossFile(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        DirectorFile* member_file) const;

    std::shared_ptr<DirectorFile> file_;
    cast::CastLibManager* cast_lib_manager_{nullptr};
    frame::FrameContext* frame_context_{nullptr};
    mutable std::shared_ptr<const bitmap::Palette> movie_palette_;
    mutable int movie_palette_frame_{-1};
};

} // namespace libreshockwave::player
