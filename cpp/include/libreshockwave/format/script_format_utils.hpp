#pragma once

#include <string>
#include <string_view>

#include "libreshockwave/chunks/script_chunk.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::format {

[[nodiscard]] std::string getLiteralTypeName(int type_code);
[[nodiscard]] std::string getLiteralTypeNameShort(int type_code);
// Formats a double the way the script literal table stores it.
[[nodiscard]] std::string floatingToString(double value);
[[nodiscard]] std::string formatLiteral(const chunks::ScriptChunk::LiteralEntry& literal);
[[nodiscard]] std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value);
[[nodiscard]] std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value, int max_string_length);
[[nodiscard]] std::string getScriptTypeName(chunks::ScriptChunkType script_type);
[[nodiscard]] std::string resolveName(const chunks::ScriptNamesChunk* names, int name_id);
[[nodiscard]] std::string resolveHandlerName(const chunks::ScriptNamesChunk* names, int name_id);
[[nodiscard]] std::string truncate(std::string_view value, int max_length);
[[nodiscard]] std::string normalizeLineEndings(std::string_view value);

} // namespace libreshockwave::format
