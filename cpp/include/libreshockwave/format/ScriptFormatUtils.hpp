#pragma once

#include <string>
#include <string_view>

#include "libreshockwave/chunks/ScriptChunk.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::format {

[[nodiscard]] std::string getLiteralTypeName(int typeCode);
[[nodiscard]] std::string getLiteralTypeNameShort(int typeCode);
[[nodiscard]] std::string formatLiteral(const chunks::ScriptChunk::LiteralEntry& literal);
[[nodiscard]] std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value);
[[nodiscard]] std::string formatLiteralValue(const chunks::ScriptChunk::LiteralValue& value, int maxStringLength);
[[nodiscard]] std::string getScriptTypeName(chunks::ScriptChunkType scriptType);
[[nodiscard]] std::string resolveName(const chunks::ScriptNamesChunk* names, int nameId);
[[nodiscard]] std::string resolveHandlerName(const chunks::ScriptNamesChunk* names, int nameId);
[[nodiscard]] std::string truncate(std::string_view value, int maxLength);
[[nodiscard]] std::string normalizeLineEndings(std::string_view value);

} // namespace libreshockwave::format
