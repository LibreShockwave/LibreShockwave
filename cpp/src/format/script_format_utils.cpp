#include "libreshockwave/format/script_format_utils.hpp"

#include <sstream>

#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/util/string_utils.hpp"

namespace libreshockwave::format {
namespace {

std::string literalValueToString(const chunks::ScriptChunk::LiteralValue& value, int max_string_length, bool truncate_strings) {
    if (std::holds_alternative<std::string>(value)) {
        auto string_value = std::get<std::string>(value);
        if (truncate_strings) {
            string_value = truncate(string_value, max_string_length);
        }
        return "\"" + string_value + "\"";
    }
    if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    }
    if (std::holds_alternative<std::vector<std::uint8_t>>(value)) {
        return "[bytes:" + std::to_string(std::get<std::vector<std::uint8_t>>(value).size()) + "]";
    }
    return "null";
}

} // namespace

std::string getLiteralTypeName(int type_code) {
    switch (type_code) {
        case 1:
            return "string";
        case 4:
            return "int";
        case 9:
            return "float";
        default:
            return "type" + std::to_string(type_code);
    }
}

std::string getLiteralTypeNameShort(int type_code) {
    switch (type_code) {
        case 1:
            return "str";
        case 4:
            return "int";
        case 9:
            return "float";
        default:
            return "lit";
    }
}

std::string formatLiteral(const chunks::ScriptChunk::LiteralEntry& literal) {
    return getLiteralTypeName(literal.type) + ": " + formatLiteralValue(literal.value);
}

std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value) {
    return literalValueToString(value, 0, false);
}

std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value, int max_string_length) {
    return literalValueToString(value, max_string_length, true);
}

std::string getScriptTypeName(chunks::ScriptChunkType script_type) {
    switch (script_type) {
        case chunks::ScriptChunkType::MovieScript:
            return "Movie Script";
        case chunks::ScriptChunkType::Behavior:
            return "Behavior";
        case chunks::ScriptChunkType::Parent:
            return "Parent Script";
        case chunks::ScriptChunkType::Score:
            return "Score Script";
        case chunks::ScriptChunkType::Unknown:
            return "Script";
    }
    return "Script";
}

std::string resolveName(const chunks::ScriptNamesChunk* names, int name_id) {
    if (names && name_id >= 0 && name_id < static_cast<int>(names->names().size())) {
        return names->getName(name_id);
    }
    return "#" + std::to_string(name_id);
}

std::string resolveHandlerName(const chunks::ScriptNamesChunk* names, int name_id) {
    if (names && name_id >= 0 && name_id < static_cast<int>(names->names().size())) {
        return names->getName(name_id);
    }
    return "handler#" + std::to_string(name_id);
}

std::string truncate(std::string_view value, int max_length) {
    return util::truncate(value, max_length);
}

std::string normalizeLineEndings(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r') {
            normalized.push_back(' ');
            if (index + 1 < value.size() && value[index + 1] == '\n') {
                ++index;
            }
        } else if (value[index] == '\n') {
            normalized.push_back(' ');
        } else {
            normalized.push_back(value[index]);
        }
    }

    const auto start = normalized.find_first_not_of(' ');
    if (start == std::string::npos) {
        return "";
    }
    const auto end = normalized.find_last_not_of(' ');
    return normalized.substr(start, end - start + 1);
}

std::string floatingToString(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

} // namespace libreshockwave::format
