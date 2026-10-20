#include "libreshockwave/cast/xmed_styled_text.hpp"

namespace libreshockwave::cast {

std::string XmedStyledText::fontStyleString() const {
    bool resolved_bold = member_bold;
    bool italic = false;
    bool underline = false;
    if (!styled_spans.empty()) {
        const auto& first = styled_spans.front();
        resolved_bold = resolved_bold || first.bold;
        italic = first.italic;
        underline = first.underline;
    }

    std::string result;
    if (resolved_bold) {
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
           (static_cast<std::uint32_t>(color_r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(color_g & 0xFF) << 8) |
           static_cast<std::uint32_t>(color_b & 0xFF);
}

} // namespace libreshockwave::cast
