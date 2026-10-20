#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/palette.hpp"

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

    Bitmap(int width, int height, int bit_depth);
    Bitmap(int width, int height, int bit_depth, std::vector<std::uint32_t> pixels);

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int bitDepth() const;
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const;
    [[nodiscard]] std::vector<std::uint32_t>& pixels();

    [[nodiscard]] bool isScriptModified() const;
    void markScriptModified();
    void clearScriptModified();
    [[nodiscard]] bool hasTransparentPixels() const;
    [[nodiscard]] bool hasTranslucentPixels() const;
    [[nodiscard]] bool hasDegenerateAlphaWithRgbContent() const;
    [[nodiscard]] bool hasNativeMatteAlpha() const;
    [[nodiscard]] bool isNativeAlpha() const;
    void setNativeAlpha(bool native_alpha);
    [[nodiscard]] bool isRectangularMedia() const;
    void setRectangularMedia(bool rectangular_media);
    [[nodiscard]] bool isTextRendered() const;
    void markTextRendered();
    void clearTextRendered();
    [[nodiscard]] bool hasScriptFillBacking() const;
    void markScriptFillBacking();
    void markScriptFillBacking(std::uint32_t argb);
    void clearScriptFillBacking();
    [[nodiscard]] std::optional<std::uint32_t> scriptFillBackingColor() const;
    [[nodiscard]] bool preservesScriptFillBacking() const;
    void markPreserveScriptFillBacking();
    void clearPreserveScriptFillBacking();

    [[nodiscard]] Bitmap copyWithNonNativeAlphaOpaque() const;
    [[nodiscard]] Bitmap copyWithDegenerateAlphaOpaque() const;
    [[nodiscard]] Bitmap copyWithDegenerateNativeAlphaOpaque() const;

    void setImagePalette(std::shared_ptr<const Palette> palette);
    void setImagePalette(const Palette* palette);
    [[nodiscard]] std::shared_ptr<const Palette> imagePalette() const;

    void setPaletteIndices(std::vector<std::uint8_t> palette_indices);
    void clearPaletteIndices();
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> paletteIndices() const;
    [[nodiscard]] std::optional<int> paletteIndex(int x, int y) const;
    void setPixelPreservePaletteIndex(int x, int y, std::uint32_t argb);
    void fillRectPaletteIndex(int x, int y, int w, int h, int index, std::uint32_t argb);
    [[nodiscard]] int remapImagePalette(std::shared_ptr<const Palette> new_palette);
    [[nodiscard]] int remapImagePalette(const Palette* new_palette);

    void setPaletteRefCastMember(int cast_lib_number, int member_number);
    [[nodiscard]] int paletteRefCastLib() const;
    [[nodiscard]] int paletteRefMemberNum() const;
    void setPaletteRefSystemName(std::string system_name);
    [[nodiscard]] const std::optional<std::string>& paletteRefSystemName() const;
    void clearPaletteRefMetadata();

    void setAnchorPoint(int x, int y);
    [[nodiscard]] bool hasAnchorPoint() const;
    [[nodiscard]] int anchorX() const;
    [[nodiscard]] int anchorY() const;
    void clearAnchorPoint();

    void copyPaletteMetadataFrom(const Bitmap* other);
    void copyPaletteStateFrom(const Bitmap* other);
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

    [[nodiscard]] static Bitmap createPaletteSwatch(const std::vector<std::uint32_t>& colors, int swatch_size, int columns);
    [[nodiscard]] static Bitmap createPaletteSwatch(const Palette& palette, int swatch_size);

private:
    [[nodiscard]] int quantizeToImagePalette();
    [[nodiscard]] std::uint32_t quantizeArgb(std::uint32_t argb) const;
    [[nodiscard]] bool shouldQuantizeRgbFills() const;
    [[nodiscard]] static int clamp(int value, int min, int max);

    int width_;
    int height_;
    int bit_depth_;
    std::vector<std::uint32_t> pixels_;
    std::optional<std::vector<std::uint8_t>> palette_indices_;
    bool script_modified_ = false;
    bool native_alpha_ = false;
    bool rectangular_media_ = false;
    bool text_rendered_ = false;
    bool script_fill_backing_ = false;
    std::optional<std::uint32_t> script_fill_backing_color_;
    bool preserve_script_fill_backing_ = false;
    std::shared_ptr<const Palette> image_palette_;
    int palette_ref_cast_lib_ = -1;
    int palette_ref_member_num_ = -1;
    std::optional<std::string> palette_ref_system_name_;
    bool has_anchor_point_ = false;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
};

} // namespace libreshockwave::bitmap
