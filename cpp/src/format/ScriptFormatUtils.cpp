#include "libreshockwave/format/ScriptFormatUtils.hpp"

#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/util/StringUtils.hpp"

namespace libreshockwave::format {
namespace {

std::string literalValueToString(const chunks::ScriptChunk::LiteralValue& value, int maxStringLength, bool truncateStrings) {
    if (std::holds_alternative<std::string>(value)) {
        auto stringValue = std::get<std::string>(value);
        if (truncateStrings) {
            stringValue = truncate(stringValue, maxStringLength);
        }
        return "\"" + stringValue + "\"";
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

std::string getLiteralTypeName(int typeCode) {
    switch (typeCode) {
        case 1:
            return "string";
        case 4:
            return "int";
        case 9:
            return "float";
        default:
            return "type" + std::to_string(typeCode);
    }
}

std::string getLiteralTypeNameShort(int typeCode) {
    switch (typeCode) {
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

std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value, int maxStringLength) {
    return literalValueToString(value, maxStringLength, true);
}

std::string getScriptTypeName(chunks::ScriptChunkType scriptType) {
    switch (scriptType) {
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

std::string resolveName(const chunks::ScriptNamesChunk* names, int nameId) {
    if (names && nameId >= 0 && nameId < static_cast<int>(names->names().size())) {
        return names->getName(nameId);
    }
    return "#" + std::to_string(nameId);
}

std::string resolveHandlerName(const chunks::ScriptNamesChunk* names, int nameId) {
    if (names && nameId >= 0 && nameId < static_cast<int>(names->names().size())) {
        return names->getName(nameId);
    }
    return "handler#" + std::to_string(nameId);
}

std::string truncate(std::string_view value, int maxLength) {
    return util::truncate(value, maxLength);
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

} // namespace libreshockwave::format
