#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/cast/styled_span.hpp"

namespace libreshockwave::cast {

struct XmedStyledText {
    std::string text;
    std::vector<StyledSpan> styled_spans;
    std::vector<std::string> font_candidates;
    std::string alignment;
    int primary_paragraph_style_index;
    int primary_paragraph_alignment_code;
    int paragraph_style_count;
    bool word_wrap;
    int fixed_line_space;
    int width;
    int height;
    std::string font_name;
    int font_size;
    bool antialias;
    int anti_alias_threshold;
    bool member_bold;
    int color_r;
    int color_g;
    int color_b;

    [[nodiscard]] std::string fontStyleString() const;
    [[nodiscard]] std::uint32_t textColorARGB() const;
};

} // namespace libreshockwave::cast
