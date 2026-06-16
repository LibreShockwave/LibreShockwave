#include "libreshockwave/util/StringUtils.hpp"

namespace libreshockwave::util {

std::string truncate(std::string_view value, int maxLength) {
    if (maxLength >= 0 && value.size() <= static_cast<std::size_t>(maxLength)) {
        return std::string(value);
    }
    if (maxLength <= 3) {
        return "...";
    }
    return std::string(value.substr(0, static_cast<std::size_t>(maxLength - 3))) + "...";
}

std::string escapeForDisplay(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string escapeHtml(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

} // namespace libreshockwave::util
