#include "libreshockwave/bitmap/bitmap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace libreshockwave::bitmap {
namespace {

std::shared_ptr<const Palette> borrowedPalette(const Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const Palette>(palette, [](const Palette*) {});
}

} // namespace

Bitmap::Bitmap(int width, int height, int bit_depth)
    : width_(width),
      height_(height),
      bit_depth_(bit_depth),
      pixels_(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 0) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("Bitmap dimensions must be non-negative");
    }
}

Bitmap::Bitmap(int width, int height, int bit_depth, std::vector<std::uint32_t> pixels)
    : width_(width), height_(height), bit_depth_(bit_depth), pixels_(std::move(pixels)) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("Bitmap dimensions must be non-negative");
    }
    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels_.size() != expected) {
        throw std::invalid_argument("Bitmap pixel count does not match dimensions");
    }
}

int Bitmap::width() const { return width_; }
int Bitmap::height() const { return height_; }
int Bitmap::bitDepth() const { return bit_depth_; }

const std::vector<std::uint32_t>& Bitmap::pixels() const { return pixels_; }
std::vector<std::uint32_t>& Bitmap::pixels() { return pixels_; }

bool Bitmap::isScriptModified() const { return script_modified_; }
void Bitmap::markScriptModified() { script_modified_ = true; }
void Bitmap::clearScriptModified() { script_modified_ = false; }

bool Bitmap::hasTransparentPixels() const {
    if (bit_depth_ < 32) {
        return false;
    }
    return std::any_of(pixels_.begin(), pixels_.end(), [](std::uint32_t pixel) {
        return (pixel >> 24) == 0;
    });
}

bool Bitmap::hasTranslucentPixels() const {
    if (bit_depth_ < 32) {
        return false;
    }
    return std::any_of(pixels_.begin(), pixels_.end(), [](std::uint32_t pixel) {
        const auto alpha = (pixel >> 24) & 0xFF;
        return alpha > 0 && alpha < 255;
    });
}

bool Bitmap::hasDegenerateAlphaWithRgbContent() const {
    if (bit_depth_ != 32 || pixels_.empty()) {
        return false;
    }

    bool has_rgb_content = false;
    for (const auto pixel : pixels_) {
        const auto alpha = (pixel >> 24U) & 0xFFU;
        if (alpha > 1U) {
            return false;
        }
        has_rgb_content = has_rgb_content || ((pixel & 0x00FFFFFFU) != 0U);
    }
    return has_rgb_content;
}

bool Bitmap::hasNativeMatteAlpha() const {
    return bit_depth_ == 32 && native_alpha_;
}

bool Bitmap::isNativeAlpha() const { return native_alpha_; }
void Bitmap::setNativeAlpha(bool native_alpha) { native_alpha_ = native_alpha; }

bool Bitmap::isRectangularMedia() const { return rectangular_media_; }
void Bitmap::setRectangularMedia(bool rectangular_media) { rectangular_media_ = rectangular_media; }

bool Bitmap::isTextRendered() const { return text_rendered_; }
void Bitmap::markTextRendered() { text_rendered_ = true; }
void Bitmap::clearTextRendered() { text_rendered_ = false; }

bool Bitmap::hasScriptFillBacking() const { return script_fill_backing_; }
void Bitmap::markScriptFillBacking() {
    script_fill_backing_ = true;
    script_fill_backing_color_.reset();
}
void Bitmap::markScriptFillBacking(std::uint32_t argb) {
    script_fill_backing_ = true;
    script_fill_backing_color_ = argb;
}
void Bitmap::clearScriptFillBacking() {
    script_fill_backing_ = false;
    script_fill_backing_color_.reset();
}
std::optional<std::uint32_t> Bitmap::scriptFillBackingColor() const { return script_fill_backing_color_; }

bool Bitmap::preservesScriptFillBacking() const { return preserve_script_fill_backing_; }
void Bitmap::markPreserveScriptFillBacking() { preserve_script_fill_backing_ = true; }
void Bitmap::clearPreserveScriptFillBacking() { preserve_script_fill_backing_ = false; }

Bitmap Bitmap::copyWithNonNativeAlphaOpaque() const {
    if (bit_depth_ != 32 || native_alpha_ || !hasTransparentPixels()) {
        return *this;
    }

    Bitmap opaque = copy();
    for (auto& pixel : opaque.pixels_) {
        pixel = 0xFF000000U | (pixel & 0x00FFFFFFU);
    }
    opaque.native_alpha_ = false;
    return opaque;
}

Bitmap Bitmap::copyWithDegenerateAlphaOpaque() const {
    if (!hasDegenerateAlphaWithRgbContent()) {
        return *this;
    }

    Bitmap opaque = copy();
    for (auto& pixel : opaque.pixels_) {
        pixel = 0xFF000000U | (pixel & 0x00FFFFFFU);
    }
    opaque.native_alpha_ = false;
    return opaque;
}

Bitmap Bitmap::copyWithDegenerateNativeAlphaOpaque() const {
    if (!native_alpha_) {
        return *this;
    }
    return copyWithDegenerateAlphaOpaque();
}

void Bitmap::setImagePalette(std::shared_ptr<const Palette> palette) {
    image_palette_ = std::move(palette);
}

void Bitmap::setImagePalette(const Palette* palette) {
    image_palette_ = borrowedPalette(palette);
}

std::shared_ptr<const Palette> Bitmap::imagePalette() const {
    return image_palette_;
}

void Bitmap::setPaletteIndices(std::vector<std::uint8_t> palette_indices) {
    palette_indices_ = std::move(palette_indices);
}

void Bitmap::clearPaletteIndices() {
    palette_indices_.reset();
}

std::optional<std::vector<std::uint8_t>> Bitmap::paletteIndices() const {
    return palette_indices_;
}

std::optional<int> Bitmap::paletteIndex(int x, int y) const {
    if (!palette_indices_.has_value() || palette_indices_->size() != pixels_.size() ||
        x < 0 || x >= width_ || y < 0 || y >= height_) {
        return std::nullopt;
    }
    return static_cast<int>((*palette_indices_)[static_cast<std::size_t>(y * width_ + x)]);
}

void Bitmap::setPixelPreservePaletteIndex(int x, int y, std::uint32_t argb) {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        pixels_[static_cast<std::size_t>(y * width_ + x)] = argb;
    }
}

void Bitmap::fillRectPaletteIndex(int x, int y, int w, int h, int index, std::uint32_t argb) {
    if (!palette_indices_.has_value() || palette_indices_->size() != pixels_.size()) {
        palette_indices_ = std::vector<std::uint8_t>(pixels_.size(), 0);
    }

    const int x2 = std::min(x + w, width_);
    const int y2 = std::min(y + h, height_);
    x = std::max(0, x);
    y = std::max(0, y);
    if (x >= x2 || y >= y2) {
        return;
    }

    for (int py = y; py < y2; ++py) {
        for (int px = x; px < x2; ++px) {
            const auto offset = static_cast<std::size_t>(py * width_ + px);
            pixels_[offset] = argb;
            (*palette_indices_)[offset] = static_cast<std::uint8_t>(index & 0xFF);
        }
    }
}

int Bitmap::remapImagePalette(std::shared_ptr<const Palette> new_palette) {
    const auto old_palette = image_palette_;
    image_palette_ = std::move(new_palette);

    if (!image_palette_ || old_palette.get() == image_palette_.get()) {
        return 0;
    }

    if (palette_indices_.has_value() && palette_indices_->size() == pixels_.size()) {
        int changed = 0;
        const int max = image_palette_->size();
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            const auto alpha = (pixels_[i] >> 24) & 0xFF;
            if (alpha == 0) {
                continue;
            }
            const int index = (*palette_indices_)[i] & 0xFF;
            if (index >= max) {
                continue;
            }
            const auto new_rgb = image_palette_->getColor(index) & 0xFFFFFFU;
            if ((pixels_[i] & 0xFFFFFFU) != new_rgb) {
                pixels_[i] = (alpha << 24) | new_rgb;
                ++changed;
            }
        }
        return changed;
    }

    if (!old_palette) {
        return shouldQuantizeRgbFills() ? quantizeToImagePalette() : 0;
    }

    int changed = 0;
    const int max = std::min(old_palette->size(), image_palette_->size());
    for (auto& pixel : pixels_) {
        const auto alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0) {
            continue;
        }
        const auto rgb = pixel & 0xFFFFFFU;
        for (int index = 0; index < max; ++index) {
            if ((old_palette->getColor(index) & 0xFFFFFFU) == rgb) {
                const auto new_rgb = image_palette_->getColor(index) & 0xFFFFFFU;
                if (new_rgb != rgb) {
                    pixel = (alpha << 24) | new_rgb;
                    ++changed;
                }
                break;
            }
        }
    }
    return changed;
}

int Bitmap::remapImagePalette(const Palette* new_palette) {
    return remapImagePalette(borrowedPalette(new_palette));
}

void Bitmap::setPaletteRefCastMember(int cast_lib_number, int member_number) {
    palette_ref_cast_lib_ = cast_lib_number;
    palette_ref_member_num_ = member_number;
    palette_ref_system_name_.reset();
}

int Bitmap::paletteRefCastLib() const { return palette_ref_cast_lib_; }
int Bitmap::paletteRefMemberNum() const { return palette_ref_member_num_; }

void Bitmap::setPaletteRefSystemName(std::string system_name) {
    palette_ref_system_name_ = std::move(system_name);
    palette_ref_cast_lib_ = -1;
    palette_ref_member_num_ = -1;
}

const std::optional<std::string>& Bitmap::paletteRefSystemName() const {
    return palette_ref_system_name_;
}

void Bitmap::clearPaletteRefMetadata() {
    palette_ref_cast_lib_ = -1;
    palette_ref_member_num_ = -1;
    palette_ref_system_name_.reset();
}

void Bitmap::setAnchorPoint(int x, int y) {
    has_anchor_point_ = true;
    anchor_x_ = x;
    anchor_y_ = y;
}

bool Bitmap::hasAnchorPoint() const { return has_anchor_point_; }
int Bitmap::anchorX() const { return anchor_x_; }
int Bitmap::anchorY() const { return anchor_y_; }

void Bitmap::clearAnchorPoint() {
    has_anchor_point_ = false;
    anchor_x_ = 0;
    anchor_y_ = 0;
}

void Bitmap::copyPaletteMetadataFrom(const Bitmap* other) {
    if (other == nullptr) {
        image_palette_.reset();
        palette_indices_.reset();
        script_modified_ = false;
        native_alpha_ = false;
        rectangular_media_ = false;
        text_rendered_ = false;
        script_fill_backing_ = false;
        script_fill_backing_color_.reset();
        preserve_script_fill_backing_ = false;
        clearPaletteRefMetadata();
        clearAnchorPoint();
        return;
    }

    image_palette_ = other->image_palette_;
    palette_indices_ = other->palette_indices_;
    script_modified_ = other->script_modified_;
    native_alpha_ = other->native_alpha_;
    rectangular_media_ = other->rectangular_media_;
    text_rendered_ = other->text_rendered_;
    script_fill_backing_ = other->script_fill_backing_;
    script_fill_backing_color_ = other->script_fill_backing_color_;
    preserve_script_fill_backing_ = other->preserve_script_fill_backing_;
    palette_ref_cast_lib_ = other->palette_ref_cast_lib_;
    palette_ref_member_num_ = other->palette_ref_member_num_;
    palette_ref_system_name_ = other->palette_ref_system_name_;
    has_anchor_point_ = other->has_anchor_point_;
    anchor_x_ = other->anchor_x_;
    anchor_y_ = other->anchor_y_;
}

void Bitmap::copyPaletteStateFrom(const Bitmap* other) {
    if (other == nullptr) {
        image_palette_.reset();
        palette_indices_.reset();
        clearPaletteRefMetadata();
        return;
    }

    image_palette_ = other->image_palette_;
    // Per-pixel indices only transfer when the pixel counts match; a size
    // mismatch would leave this bitmap's indices misaligned with its pixels.
    if (other->palette_indices_.has_value() &&
        other->palette_indices_->size() == pixels_.size()) {
        palette_indices_ = other->palette_indices_;
    } else {
        palette_indices_.reset();
    }
    palette_ref_cast_lib_ = other->palette_ref_cast_lib_;
    palette_ref_member_num_ = other->palette_ref_member_num_;
    palette_ref_system_name_ = other->palette_ref_system_name_;
}

std::uint32_t Bitmap::resolvePaletteIndex(int index, const Palette* fallback) const {
    const Palette* palette = image_palette_ ? image_palette_.get() : fallback;
    if (palette != nullptr) {
        return palette->getColor(index & 0xFF);
    }
    const auto gray = static_cast<std::uint32_t>(255 - (index & 0xFF));
    return (gray << 16) | (gray << 8) | gray;
}

std::uint32_t Bitmap::getPixel(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return 0;
    }
    return pixels_[static_cast<std::size_t>(y * width_ + x)];
}

void Bitmap::setPixel(int x, int y, std::uint32_t argb) {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        if (shouldQuantizeRgbFills()) {
            fillRectPaletteIndex(x, y, 1, 1, image_palette_->nearestIndex(argb), quantizeArgb(argb));
            return;
        }
        clearPaletteIndices();
        pixels_[static_cast<std::size_t>(y * width_ + x)] = argb;
    }
}

void Bitmap::setPixelRGB(int x, int y, int r, int g, int b) {
    setPixel(x, y, 0xFF000000U |
                   (static_cast<std::uint32_t>(r & 0xFF) << 16) |
                   (static_cast<std::uint32_t>(g & 0xFF) << 8) |
                   static_cast<std::uint32_t>(b & 0xFF));
}

void Bitmap::setPixelRGBA(int x, int y, int r, int g, int b, int a) {
    setPixel(x, y,
             (static_cast<std::uint32_t>(a & 0xFF) << 24) |
             (static_cast<std::uint32_t>(r & 0xFF) << 16) |
             (static_cast<std::uint32_t>(g & 0xFF) << 8) |
             static_cast<std::uint32_t>(b & 0xFF));
}

void Bitmap::fill(std::uint32_t argb) {
    if (shouldQuantizeRgbFills()) {
        fillRectPaletteIndex(0, 0, width_, height_, image_palette_->nearestIndex(argb), quantizeArgb(argb));
        return;
    }
    clearPaletteIndices();
    std::fill(pixels_.begin(), pixels_.end(), argb);
}

void Bitmap::fillRect(int x, int y, int w, int h, std::uint32_t argb) {
    if (shouldQuantizeRgbFills()) {
        fillRectPaletteIndex(x, y, w, h, image_palette_->nearestIndex(argb), quantizeArgb(argb));
        return;
    }

    clearPaletteIndices();
    const int x2 = std::min(x + w, width_);
    const int y2 = std::min(y + h, height_);
    x = std::max(0, x);
    y = std::max(0, y);
    if (x >= x2 || y >= y2) {
        return;
    }

    for (int py = y; py < y2; ++py) {
        for (int px = x; px < x2; ++px) {
            pixels_[static_cast<std::size_t>(py * width_ + px)] = argb;
        }
    }
}

Bitmap Bitmap::copy() const {
    Bitmap result(width_, height_, bit_depth_, pixels_);
    result.copyPaletteMetadataFrom(this);
    return result;
}

Bitmap Bitmap::getRegion(int x, int y, int w, int h) const {
    Bitmap result(w, h, bit_depth_);
    result.copyPaletteMetadataFrom(this);
    std::optional<std::vector<std::uint8_t>> region_indices;
    if (palette_indices_.has_value()) {
        region_indices = std::vector<std::uint8_t>(static_cast<std::size_t>(w * h), 0);
    }

    for (int dy = 0; dy < h; ++dy) {
        const int src_y = y + dy;
        if (src_y < 0 || src_y >= height_) {
            continue;
        }
        for (int dx = 0; dx < w; ++dx) {
            const int src_x = x + dx;
            if (src_x < 0 || src_x >= width_) {
                continue;
            }
            const auto dst_offset = static_cast<std::size_t>(dy * w + dx);
            const auto src_offset = static_cast<std::size_t>(src_y * width_ + src_x);
            result.pixels_[dst_offset] = pixels_[src_offset];
            if (region_indices.has_value() && palette_indices_.has_value() && src_offset < palette_indices_->size()) {
                (*region_indices)[dst_offset] = (*palette_indices_)[src_offset];
            }
        }
    }

    if (region_indices.has_value()) {
        result.palette_indices_ = std::move(region_indices);
    }
    if (has_anchor_point_) {
        result.setAnchorPoint(anchor_x_ - x, anchor_y_ - y);
    }
    return result;
}

Bitmap::Rect Bitmap::trimWhiteSpace() const {
    int min_x = width_;
    int min_y = height_;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const auto pixel = pixels_[static_cast<std::size_t>(y * width_ + x)];
            const auto alpha = pixel >> 24;
            if (alpha == 0) {
                continue;
            }
            const auto rgb = pixel & 0xFFFFFFU;
            if (rgb == 0xFFFFFFU) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    if (max_x < 0) {
        return Rect{0, 0, 0, 0};
    }
    return Rect{min_x, min_y, max_x + 1, max_y + 1};
}

std::string Bitmap::toString() const {
    return "Bitmap[" + std::to_string(width_) + "x" + std::to_string(height_) + ", " +
           std::to_string(bit_depth_) + "-bit]";
}

Bitmap Bitmap::createPaletteSwatch(const std::vector<std::uint32_t>& colors, int swatch_size, int columns) {
    if (colors.empty()) {
        return Bitmap(1, 1, 32);
    }
    const int count = static_cast<int>(colors.size());
    const int cols = columns > 0 ? columns : static_cast<int>(std::ceil(std::sqrt(count)));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));

    Bitmap bitmap(cols * swatch_size, rows * swatch_size, 32);
    bitmap.fill(0xFFFFFFFFU);
    for (int i = 0; i < count; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        bitmap.fillRect(col * swatch_size, row * swatch_size, swatch_size, swatch_size,
                        0xFF000000U | (colors[static_cast<std::size_t>(i)] & 0xFFFFFFU));
    }
    return bitmap;
}

Bitmap Bitmap::createPaletteSwatch(const Palette& palette, int swatch_size) {
    return createPaletteSwatch(palette.colors(), swatch_size, 16);
}

int Bitmap::quantizeToImagePalette() {
    if (!image_palette_) {
        return 0;
    }
    if (!palette_indices_.has_value() || palette_indices_->size() != pixels_.size()) {
        palette_indices_ = std::vector<std::uint8_t>(pixels_.size(), 0);
    }

    int changed = 0;
    for (std::size_t i = 0; i < pixels_.size(); ++i) {
        const auto pixel = pixels_[i];
        const auto alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0) {
            continue;
        }
        const int index = image_palette_->nearestIndex(pixel);
        const auto quantized = (alpha << 24) | (image_palette_->getColor(index) & 0xFFFFFFU);
        (*palette_indices_)[i] = static_cast<std::uint8_t>(index & 0xFF);
        if (pixels_[i] != quantized) {
            pixels_[i] = quantized;
            ++changed;
        }
    }
    return changed;
}

std::uint32_t Bitmap::quantizeArgb(std::uint32_t argb) const {
    if (!image_palette_) {
        return argb;
    }
    const auto alpha = (argb >> 24) & 0xFF;
    const int index = image_palette_->nearestIndex(argb);
    return (alpha << 24) | (image_palette_->getColor(index) & 0xFFFFFFU);
}

bool Bitmap::shouldQuantizeRgbFills() const {
    return bit_depth_ <= 8 && image_palette_ != nullptr;
}

int Bitmap::clamp(int value, int min, int max) {
    return std::max(min, std::min(max, value));
}

} // namespace libreshockwave::bitmap
