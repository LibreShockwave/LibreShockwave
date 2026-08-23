#pragma once

#include <optional>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::player::input {

/// Screen-space caret line for the focused editable text field.
/// Coordinates are stage pixels; height is the field's line height.
struct CaretInfo {
    int x{0};
    int y{0};
    int height{0};

    friend bool operator==(const CaretInfo&, const CaretInfo&) = default;
};

/// One rectangular span of selected text inside an editable field.
/// Multi-line selections produce one rect per visual line.
struct SelectionRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    friend bool operator==(const SelectionRect&, const SelectionRect&) = default;
};

/// Host-presented editing chrome captured alongside a frame snapshot.
/// Example: auto overlay = handler.editableFieldOverlay(); then paint it onto
/// the composed frame with applyEditableFieldOverlay before showing the frame
/// to the user. Never baked into Lingo-visible stage images.
struct EditableFieldOverlay {
    std::optional<CaretInfo> caret;
    std::vector<SelectionRect> selectionRects;

    [[nodiscard]] bool empty() const {
        return !caret.has_value() && selectionRects.empty();
    }

    friend bool operator==(const EditableFieldOverlay&, const EditableFieldOverlay&) = default;
};

/// Paint caret and selection chrome onto a composed stage bitmap in place.
/// The caret draws as a black column; selection rects invert their pixels.
/// Rects outside the bitmap are clipped; empty overlays leave it untouched.
void applyEditableFieldOverlay(bitmap::Bitmap& bitmap, const EditableFieldOverlay& overlay);

/// Copy of `bitmap` with applyEditableFieldOverlay painted onto it.
[[nodiscard]] bitmap::Bitmap withEditableFieldOverlay(const bitmap::Bitmap& bitmap,
                                                      const EditableFieldOverlay& overlay);

} // namespace libreshockwave::player::input
