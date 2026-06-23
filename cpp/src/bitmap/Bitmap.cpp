#include "libreshockwave/bitmap/Bitmap.hpp"

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

Bitmap::Bitmap(int width, int height, int bitDepth)
    : width_(width),
      height_(height),
      bitDepth_(bitDepth),
      pixels_(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 0) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("Bitmap dimensions must be non-negative");
    }
}

Bitmap::Bitmap(int width, int height, int bitDepth, std::vector<std::uint32_t> pixels)
    : width_(width), height_(height), bitDepth_(bitDepth), pixels_(std::move(pixels)) {
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
int Bitmap::bitDepth() const { return bitDepth_; }

const std::vector<std::uint32_t>& Bitmap::pixels() const { return pixels_; }
std::vector<std::uint32_t>& Bitmap::pixels() { return pixels_; }

bool Bitmap::isScriptModified() const { return scriptModified_; }
void Bitmap::markScriptModified() { scriptModified_ = true; }
void Bitmap::clearScriptModified() { scriptModified_ = false; }

bool Bitmap::hasTransparentPixels() const {
    if (bitDepth_ < 32) {
        return false;
    }
    return std::any_of(pixels_.begin(), pixels_.end(), [](std::uint32_t pixel) {
        return (pixel >> 24) == 0;
    });
}

bool Bitmap::hasTranslucentPixels() const {
    if (bitDepth_ < 32) {
        return false;
    }
    return std::any_of(pixels_.begin(), pixels_.end(), [](std::uint32_t pixel) {
        const auto alpha = (pixel >> 24) & 0xFF;
        return alpha > 0 && alpha < 255;
    });
}

bool Bitmap::hasDegenerateAlphaWithRgbContent() const {
    if (bitDepth_ != 32 || pixels_.empty()) {
        return false;
    }

    bool hasRgbContent = false;
    for (const auto pixel : pixels_) {
        const auto alpha = (pixel >> 24U) & 0xFFU;
        if (alpha > 1U) {
            return false;
        }
        hasRgbContent = hasRgbContent || ((pixel & 0x00FFFFFFU) != 0U);
    }
    return hasRgbContent;
}

bool Bitmap::hasNativeMatteAlpha() const {
    return bitDepth_ == 32 && nativeAlpha_;
}

bool Bitmap::isNativeAlpha() const { return nativeAlpha_; }
void Bitmap::setNativeAlpha(bool nativeAlpha) { nativeAlpha_ = nativeAlpha; }

bool Bitmap::isRectangularMedia() const { return rectangularMedia_; }
void Bitmap::setRectangularMedia(bool rectangularMedia) { rectangularMedia_ = rectangularMedia; }

bool Bitmap::isTextRendered() const { return textRendered_; }
void Bitmap::markTextRendered() { textRendered_ = true; }
void Bitmap::clearTextRendered() { textRendered_ = false; }

bool Bitmap::hasScriptFillBacking() const { return scriptFillBacking_; }
void Bitmap::markScriptFillBacking() { scriptFillBacking_ = true; }
void Bitmap::clearScriptFillBacking() { scriptFillBacking_ = false; }

bool Bitmap::preservesScriptFillBacking() const { return preserveScriptFillBacking_; }
void Bitmap::markPreserveScriptFillBacking() { preserveScriptFillBacking_ = true; }
void Bitmap::clearPreserveScriptFillBacking() { preserveScriptFillBacking_ = false; }

Bitmap Bitmap::copyWithNonNativeAlphaOpaque() const {
    if (bitDepth_ != 32 || nativeAlpha_ || !hasTransparentPixels()) {
        return *this;
    }

    Bitmap opaque = copy();
    for (auto& pixel : opaque.pixels_) {
        pixel = 0xFF000000U | (pixel & 0x00FFFFFFU);
    }
    opaque.nativeAlpha_ = false;
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
    opaque.nativeAlpha_ = false;
    return opaque;
}

Bitmap Bitmap::copyWithDegenerateNativeAlphaOpaque() const {
    if (!nativeAlpha_) {
        return *this;
    }
    return copyWithDegenerateAlphaOpaque();
}

void Bitmap::setImagePalette(std::shared_ptr<const Palette> palette) {
    imagePalette_ = std::move(palette);
}

void Bitmap::setImagePalette(const Palette* palette) {
    imagePalette_ = borrowedPalette(palette);
}

std::shared_ptr<const Palette> Bitmap::imagePalette() const {
    return imagePalette_;
}

void Bitmap::setPaletteIndices(std::vector<std::uint8_t> paletteIndices) {
    paletteIndices_ = std::move(paletteIndices);
}

void Bitmap::clearPaletteIndices() {
    paletteIndices_.reset();
}

std::optional<std::vector<std::uint8_t>> Bitmap::paletteIndices() const {
    return paletteIndices_;
}

std::optional<int> Bitmap::paletteIndex(int x, int y) const {
    if (!paletteIndices_.has_value() || paletteIndices_->size() != pixels_.size() ||
        x < 0 || x >= width_ || y < 0 || y >= height_) {
        return std::nullopt;
    }
    return static_cast<int>((*paletteIndices_)[static_cast<std::size_t>(y * width_ + x)]);
}

void Bitmap::setPixelPreservePaletteIndex(int x, int y, std::uint32_t argb) {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        pixels_[static_cast<std::size_t>(y * width_ + x)] = argb;
    }
}

void Bitmap::fillRectPaletteIndex(int x, int y, int w, int h, int index, std::uint32_t argb) {
    if (!paletteIndices_.has_value() || paletteIndices_->size() != pixels_.size()) {
        paletteIndices_ = std::vector<std::uint8_t>(pixels_.size(), 0);
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
            (*paletteIndices_)[offset] = static_cast<std::uint8_t>(index & 0xFF);
        }
    }
}

int Bitmap::remapImagePalette(std::shared_ptr<const Palette> newPalette) {
    const auto oldPalette = imagePalette_;
    imagePalette_ = std::move(newPalette);

    if (!imagePalette_ || oldPalette.get() == imagePalette_.get()) {
        return 0;
    }

    if (paletteIndices_.has_value() && paletteIndices_->size() == pixels_.size()) {
        int changed = 0;
        const int max = imagePalette_->size();
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            const auto alpha = (pixels_[i] >> 24) & 0xFF;
            if (alpha == 0) {
                continue;
            }
            const int index = (*paletteIndices_)[i] & 0xFF;
            if (index >= max) {
                continue;
            }
            const auto newRgb = imagePalette_->getColor(index) & 0xFFFFFFU;
            if ((pixels_[i] & 0xFFFFFFU) != newRgb) {
                pixels_[i] = (alpha << 24) | newRgb;
                ++changed;
            }
        }
        return changed;
    }

    if (!oldPalette) {
        return shouldQuantizeRgbFills() ? quantizeToImagePalette() : 0;
    }

    int changed = 0;
    const int max = std::min(oldPalette->size(), imagePalette_->size());
    for (auto& pixel : pixels_) {
        const auto alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0) {
            continue;
        }
        const auto rgb = pixel & 0xFFFFFFU;
        for (int index = 0; index < max; ++index) {
            if ((oldPalette->getColor(index) & 0xFFFFFFU) == rgb) {
                const auto newRgb = imagePalette_->getColor(index) & 0xFFFFFFU;
                if (newRgb != rgb) {
                    pixel = (alpha << 24) | newRgb;
                    ++changed;
                }
                break;
            }
        }
    }
    return changed;
}

int Bitmap::remapImagePalette(const Palette* newPalette) {
    return remapImagePalette(borrowedPalette(newPalette));
}

void Bitmap::setPaletteRefCastMember(int castLibNumber, int memberNumber) {
    paletteRefCastLib_ = castLibNumber;
    paletteRefMemberNum_ = memberNumber;
    paletteRefSystemName_.reset();
}

int Bitmap::paletteRefCastLib() const { return paletteRefCastLib_; }
int Bitmap::paletteRefMemberNum() const { return paletteRefMemberNum_; }

void Bitmap::setPaletteRefSystemName(std::string systemName) {
    paletteRefSystemName_ = std::move(systemName);
    paletteRefCastLib_ = -1;
    paletteRefMemberNum_ = -1;
}

const std::optional<std::string>& Bitmap::paletteRefSystemName() const {
    return paletteRefSystemName_;
}

void Bitmap::clearPaletteRefMetadata() {
    paletteRefCastLib_ = -1;
    paletteRefMemberNum_ = -1;
    paletteRefSystemName_.reset();
}

void Bitmap::setAnchorPoint(int x, int y) {
    hasAnchorPoint_ = true;
    anchorX_ = x;
    anchorY_ = y;
}

bool Bitmap::hasAnchorPoint() const { return hasAnchorPoint_; }
int Bitmap::anchorX() const { return anchorX_; }
int Bitmap::anchorY() const { return anchorY_; }

void Bitmap::clearAnchorPoint() {
    hasAnchorPoint_ = false;
    anchorX_ = 0;
    anchorY_ = 0;
}

void Bitmap::copyPaletteMetadataFrom(const Bitmap* other) {
    if (other == nullptr) {
        imagePalette_.reset();
        paletteIndices_.reset();
        scriptModified_ = false;
        nativeAlpha_ = false;
        rectangularMedia_ = false;
        textRendered_ = false;
        scriptFillBacking_ = false;
        preserveScriptFillBacking_ = false;
        clearPaletteRefMetadata();
        clearAnchorPoint();
        return;
    }

    imagePalette_ = other->imagePalette_;
    paletteIndices_ = other->paletteIndices_;
    scriptModified_ = other->scriptModified_;
    nativeAlpha_ = other->nativeAlpha_;
    rectangularMedia_ = other->rectangularMedia_;
    textRendered_ = other->textRendered_;
    scriptFillBacking_ = other->scriptFillBacking_;
    preserveScriptFillBacking_ = other->preserveScriptFillBacking_;
    paletteRefCastLib_ = other->paletteRefCastLib_;
    paletteRefMemberNum_ = other->paletteRefMemberNum_;
    paletteRefSystemName_ = other->paletteRefSystemName_;
    hasAnchorPoint_ = other->hasAnchorPoint_;
    anchorX_ = other->anchorX_;
    anchorY_ = other->anchorY_;
}

std::uint32_t Bitmap::resolvePaletteIndex(int index, const Palette* fallback) const {
    const Palette* palette = imagePalette_ ? imagePalette_.get() : fallback;
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
            fillRectPaletteIndex(x, y, 1, 1, imagePalette_->nearestIndex(argb), quantizeArgb(argb));
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
        fillRectPaletteIndex(0, 0, width_, height_, imagePalette_->nearestIndex(argb), quantizeArgb(argb));
        return;
    }
    clearPaletteIndices();
    std::fill(pixels_.begin(), pixels_.end(), argb);
}

void Bitmap::fillRect(int x, int y, int w, int h, std::uint32_t argb) {
    if (shouldQuantizeRgbFills()) {
        fillRectPaletteIndex(x, y, w, h, imagePalette_->nearestIndex(argb), quantizeArgb(argb));
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
    Bitmap result(width_, height_, bitDepth_, pixels_);
    result.copyPaletteMetadataFrom(this);
    return result;
}

Bitmap Bitmap::getRegion(int x, int y, int w, int h) const {
    Bitmap result(w, h, bitDepth_);
    result.copyPaletteMetadataFrom(this);
    std::optional<std::vector<std::uint8_t>> regionIndices;
    if (paletteIndices_.has_value()) {
        regionIndices = std::vector<std::uint8_t>(static_cast<std::size_t>(w * h), 0);
    }

    for (int dy = 0; dy < h; ++dy) {
        const int srcY = y + dy;
        if (srcY < 0 || srcY >= height_) {
            continue;
        }
        for (int dx = 0; dx < w; ++dx) {
            const int srcX = x + dx;
            if (srcX < 0 || srcX >= width_) {
                continue;
            }
            const auto dstOffset = static_cast<std::size_t>(dy * w + dx);
            const auto srcOffset = static_cast<std::size_t>(srcY * width_ + srcX);
            result.pixels_[dstOffset] = pixels_[srcOffset];
            if (regionIndices.has_value() && paletteIndices_.has_value() && srcOffset < paletteIndices_->size()) {
                (*regionIndices)[dstOffset] = (*paletteIndices_)[srcOffset];
            }
        }
    }

    if (regionIndices.has_value()) {
        result.paletteIndices_ = std::move(regionIndices);
    }
    if (hasAnchorPoint_) {
        result.setAnchorPoint(anchorX_ - x, anchorY_ - y);
    }
    return result;
}

Bitmap::Rect Bitmap::trimWhiteSpace() const {
    int minX = width_;
    int minY = height_;
    int maxX = -1;
    int maxY = -1;
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
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < 0) {
        return Rect{0, 0, 0, 0};
    }
    return Rect{minX, minY, maxX + 1, maxY + 1};
}

std::string Bitmap::toString() const {
    return "Bitmap[" + std::to_string(width_) + "x" + std::to_string(height_) + ", " +
           std::to_string(bitDepth_) + "-bit]";
}

Bitmap Bitmap::createPaletteSwatch(const std::vector<std::uint32_t>& colors, int swatchSize, int columns) {
    if (colors.empty()) {
        return Bitmap(1, 1, 32);
    }
    const int count = static_cast<int>(colors.size());
    const int cols = columns > 0 ? columns : static_cast<int>(std::ceil(std::sqrt(count)));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));

    Bitmap bitmap(cols * swatchSize, rows * swatchSize, 32);
    bitmap.fill(0xFFFFFFFFU);
    for (int i = 0; i < count; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        bitmap.fillRect(col * swatchSize, row * swatchSize, swatchSize, swatchSize,
                        0xFF000000U | (colors[static_cast<std::size_t>(i)] & 0xFFFFFFU));
    }
    return bitmap;
}

Bitmap Bitmap::createPaletteSwatch(const Palette& palette, int swatchSize) {
    return createPaletteSwatch(palette.colors(), swatchSize, 16);
}

int Bitmap::quantizeToImagePalette() {
    if (!imagePalette_) {
        return 0;
    }
    if (!paletteIndices_.has_value() || paletteIndices_->size() != pixels_.size()) {
        paletteIndices_ = std::vector<std::uint8_t>(pixels_.size(), 0);
    }

    int changed = 0;
    for (std::size_t i = 0; i < pixels_.size(); ++i) {
        const auto pixel = pixels_[i];
        const auto alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0) {
            continue;
        }
        const int index = imagePalette_->nearestIndex(pixel);
        const auto quantized = (alpha << 24) | (imagePalette_->getColor(index) & 0xFFFFFFU);
        (*paletteIndices_)[i] = static_cast<std::uint8_t>(index & 0xFF);
        if (pixels_[i] != quantized) {
            pixels_[i] = quantized;
            ++changed;
        }
    }
    return changed;
}

std::uint32_t Bitmap::quantizeArgb(std::uint32_t argb) const {
    if (!imagePalette_) {
        return argb;
    }
    const auto alpha = (argb >> 24) & 0xFF;
    const int index = imagePalette_->nearestIndex(argb);
    return (alpha << 24) | (imagePalette_->getColor(index) & 0xFFFFFFU);
}

bool Bitmap::shouldQuantizeRgbFills() const {
    return bitDepth_ <= 8 && imagePalette_ != nullptr;
}

int Bitmap::clamp(int value, int min, int max) {
    return std::max(min, std::min(max, value));
}

} // namespace libreshockwave::bitmap
