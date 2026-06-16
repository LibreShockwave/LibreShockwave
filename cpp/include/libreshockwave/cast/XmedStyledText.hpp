#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/cast/StyledSpan.hpp"

namespace libreshockwave::cast {

struct XmedStyledText {
    std::string text;
    std::vector<StyledSpan> styledSpans;
    std::vector<std::string> fontCandidates;
    std::string alignment;
    int primaryParagraphStyleIndex;
    int primaryParagraphAlignmentCode;
    int paragraphStyleCount;
    bool wordWrap;
    int fixedLineSpace;
    int width;
    int height;
    std::string fontName;
    int fontSize;
    bool antialias;
    int antiAliasThreshold;
    bool memberBold;
    int colorR;
    int colorG;
    int colorB;

    [[nodiscard]] std::string fontStyleString() const;
    [[nodiscard]] std::uint32_t textColorARGB() const;
};

} // namespace libreshockwave::cast
