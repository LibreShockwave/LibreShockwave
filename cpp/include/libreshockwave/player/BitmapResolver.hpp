#pragma once

#include <memory>
#include <optional>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"

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
                   cast::CastLibManager* castLibManager = nullptr,
                   frame::FrameContext* frameContext = nullptr);

    [[nodiscard]] const std::shared_ptr<DirectorFile>& file() const;
    void setCastLibManager(cast::CastLibManager* castLibManager);
    void setFrameContext(frame::FrameContext* frameContext);

    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<chunks::CastMemberChunk>& member) const;
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        const bitmap::Palette* paletteOverride) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> decodeBitmapForProvider(
        const chunks::CastMemberChunk& member,
        const bitmap::Palette* paletteOverride) const;

    [[nodiscard]] std::shared_ptr<const bitmap::Palette> getMoviePalette() const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMember(int castLib, int memberNumber) const;

private:
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveMoviePalette(int frame) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteCrossFile(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        DirectorFile* memberFile) const;

    std::shared_ptr<DirectorFile> file_;
    cast::CastLibManager* castLibManager_{nullptr};
    frame::FrameContext* frameContext_{nullptr};
    mutable std::shared_ptr<const bitmap::Palette> moviePalette_;
    mutable int moviePaletteFrame_{-1};
};

} // namespace libreshockwave::player
