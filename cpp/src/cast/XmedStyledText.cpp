#include "libreshockwave/cast/XmedStyledText.hpp"

namespace libreshockwave::cast {

std::string XmedStyledText::fontStyleString() const {
    bool resolvedBold = memberBold;
    bool italic = false;
    bool underline = false;
    if (!styledSpans.empty()) {
        const auto& first = styledSpans.front();
        resolvedBold = resolvedBold || first.bold;
        italic = first.italic;
        underline = first.underline;
    }

    std::string result;
    if (resolvedBold) {
        result += "bold";
    }
    if (italic) {
        if (!result.empty()) result += ",";
        result += "italic";
    }
    if (underline) {
        if (!result.empty()) result += ",";
        result += "underline";
    }
    return result;
}

std::uint32_t XmedStyledText::textColorARGB() const {
    return 0xFF000000U |
           (static_cast<std::uint32_t>(colorR & 0xFF) << 16) |
           (static_cast<std::uint32_t>(colorG & 0xFF) << 8) |
           static_cast<std::uint32_t>(colorB & 0xFF);
}

} // namespace libreshockwave::cast
