#include "libreshockwave/util/string_utils.hpp"

namespace libreshockwave::util {

std::string truncate(std::string_view value, int max_length) {
    if (max_length >= 0 && value.size() <= static_cast<std::size_t>(max_length)) {
        return std::string(value);
    }
    if (max_length <= 3) {
        return "...";
    }
    return std::string(value.substr(0, static_cast<std::size_t>(max_length - 3))) + "...";
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
