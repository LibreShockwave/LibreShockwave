#include "libreshockwave/player/input/EditableFieldOverlay.hpp"

#include <algorithm>

namespace libreshockwave::player::input {

void applyEditableFieldOverlay(bitmap::Bitmap& bitmap, const EditableFieldOverlay& overlay) {
    if (overlay.empty() || bitmap.width() <= 0 || bitmap.height() <= 0 || bitmap.pixels().empty()) {
        return;
    }

    auto& pixels = bitmap.pixels();
    const int width = bitmap.width();
    const int height = bitmap.height();

    for (const auto& rect : overlay.selectionRects) {
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }
        const int x0 = std::max(0, rect.x);
        const int y0 = std::max(0, rect.y);
        const int x1 = std::min(width, rect.x + rect.width);
        const int y1 = std::min(height, rect.y + rect.height);
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];
                pixel = (pixel & 0xFF000000U) | ((~pixel) & 0x00FFFFFFU);
            }
        }
    }

    if (!overlay.caret.has_value() || overlay.caret->height <= 0 ||
        overlay.caret->x < 0 || overlay.caret->x >= width) {
        return;
    }
    const int x = overlay.caret->x;
    const int y0 = std::max(0, overlay.caret->y);
    const int y1 = std::min(height, overlay.caret->y + overlay.caret->height);
    for (int y = y0; y < y1; ++y) {
        pixels[static_cast<std::size_t>(y * width + x)] = 0xFF000000U;
    }
}

bitmap::Bitmap withEditableFieldOverlay(const bitmap::Bitmap& bitmap,
                                        const EditableFieldOverlay& overlay) {
    auto result = bitmap.copy();
    applyEditableFieldOverlay(result, overlay);
    return result;
}

} // namespace libreshockwave::player::input
