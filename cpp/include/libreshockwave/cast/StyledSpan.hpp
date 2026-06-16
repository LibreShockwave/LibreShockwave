#pragma once

#include <string>

namespace libreshockwave::cast {

struct StyledSpan {
    int startOffset;
    int endOffset;
    std::string fontName;
    int fontSize;
    bool bold;
    bool italic;
    bool underline;
    int colorR;
    int colorG;
    int colorB;

    friend bool operator==(const StyledSpan&, const StyledSpan&) = default;
};

} // namespace libreshockwave::cast
