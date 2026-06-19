#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Palette.hpp"

namespace libreshockwave::bitmap {

class Bitmap {
public:
    struct Rect {
        int left;
        int top;
        int right;
        int bottom;

        friend bool operator==(const Rect&, const Rect&) = default;
    };

    Bitmap(int width, int height, int bitDepth);
    Bitmap(int width, int height, int bitDepth, std::vector<std::uint32_t> pixels);

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int bitDepth() const;
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const;
    [[nodiscard]] std::vector<std::uint32_t>& pixels();

    [[nodiscard]] bool isScriptModified() const;
    void markScriptModified();
    [[nodiscard]] bool hasTransparentPixels() const;
    [[nodiscard]] bool hasTranslucentPixels() const;
    [[nodiscard]] bool hasDegenerateAlphaWithRgbContent() const;
    [[nodiscard]] bool hasNativeMatteAlpha() const;
    [[nodiscard]] bool isNativeAlpha() const;
    void setNativeAlpha(bool nativeAlpha);
    [[nodiscard]] bool isRectangularMedia() const;
    void setRectangularMedia(bool rectangularMedia);

    [[nodiscard]] Bitmap copyWithNonNativeAlphaOpaque() const;
    [[nodiscard]] Bitmap copyWithDegenerateAlphaOpaque() const;
    [[nodiscard]] Bitmap copyWithDegenerateNativeAlphaOpaque() const;

    void setImagePalette(std::shared_ptr<const Palette> palette);
    void setImagePalette(const Palette* palette);
    [[nodiscard]] std::shared_ptr<const Palette> imagePalette() const;

    void setPaletteIndices(std::vector<std::uint8_t> paletteIndices);
    void clearPaletteIndices();
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> paletteIndices() const;
    [[nodiscard]] std::optional<int> paletteIndex(int x, int y) const;
    void setPixelPreservePaletteIndex(int x, int y, std::uint32_t argb);
    void fillRectPaletteIndex(int x, int y, int w, int h, int index, std::uint32_t argb);
    [[nodiscard]] int remapImagePalette(std::shared_ptr<const Palette> newPalette);
    [[nodiscard]] int remapImagePalette(const Palette* newPalette);

    void setPaletteRefCastMember(int castLibNumber, int memberNumber);
    [[nodiscard]] int paletteRefCastLib() const;
    [[nodiscard]] int paletteRefMemberNum() const;
    void setPaletteRefSystemName(std::string systemName);
    [[nodiscard]] const std::optional<std::string>& paletteRefSystemName() const;
    void clearPaletteRefMetadata();

    void setAnchorPoint(int x, int y);
    [[nodiscard]] bool hasAnchorPoint() const;
    [[nodiscard]] int anchorX() const;
    [[nodiscard]] int anchorY() const;
    void clearAnchorPoint();

    void copyPaletteMetadataFrom(const Bitmap* other);
    [[nodiscard]] std::uint32_t resolvePaletteIndex(int index, const Palette* fallback) const;

    [[nodiscard]] std::uint32_t getPixel(int x, int y) const;
    void setPixel(int x, int y, std::uint32_t argb);
    void setPixelRGB(int x, int y, int r, int g, int b);
    void setPixelRGBA(int x, int y, int r, int g, int b, int a);
    void fill(std::uint32_t argb);
    void fillRect(int x, int y, int w, int h, std::uint32_t argb);

    [[nodiscard]] Bitmap copy() const;
    [[nodiscard]] Bitmap getRegion(int x, int y, int w, int h) const;
    [[nodiscard]] Rect trimWhiteSpace() const;
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] static Bitmap createPaletteSwatch(const std::vector<std::uint32_t>& colors, int swatchSize, int columns);
    [[nodiscard]] static Bitmap createPaletteSwatch(const Palette& palette, int swatchSize);

private:
    [[nodiscard]] int quantizeToImagePalette();
    [[nodiscard]] std::uint32_t quantizeArgb(std::uint32_t argb) const;
    [[nodiscard]] bool shouldQuantizeRgbFills() const;
    [[nodiscard]] static int clamp(int value, int min, int max);

    int width_;
    int height_;
    int bitDepth_;
    std::vector<std::uint32_t> pixels_;
    std::optional<std::vector<std::uint8_t>> paletteIndices_;
    bool scriptModified_ = false;
    bool nativeAlpha_ = false;
    bool rectangularMedia_ = false;
    std::shared_ptr<const Palette> imagePalette_;
    int paletteRefCastLib_ = -1;
    int paletteRefMemberNum_ = -1;
    std::optional<std::string> paletteRefSystemName_;
    bool hasAnchorPoint_ = false;
    int anchorX_ = 0;
    int anchorY_ = 0;
};

} // namespace libreshockwave::bitmap
