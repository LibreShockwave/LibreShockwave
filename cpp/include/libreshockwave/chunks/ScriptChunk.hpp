#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"
#include "libreshockwave/lingo/Opcode.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class ScriptNamesChunk;

enum class ScriptChunkType {
    Score = 1,
    Behavior = 2,
    MovieScript = 3,
    Parent = 7,
    Unknown = -1
};

[[nodiscard]] ScriptChunkType scriptChunkTypeFromCode(int code);

class ScriptChunk final : public Chunk {
public:
    using LiteralValue = std::variant<std::monostate, std::string, int, std::vector<std::uint8_t>>;

    struct Instruction {
        int offset;
        lingo::Opcode opcode;
        int rawOpcode;
        int argument;

        [[nodiscard]] std::string toString() const;
    };

    struct Handler {
        int nameId;
        int handlerVectorPos;
        int bytecodeLength;
        int bytecodeOffset;
        int argCount;
        int localCount;
        int globalsCount;
        int lineCount;
        std::vector<int> argNameIds;
        std::vector<int> localNameIds;
        std::vector<Instruction> instructions;
        std::unordered_map<int, int> bytecodeIndexMap;

        [[nodiscard]] int getInstructionIndex(int offset) const;
    };

    struct LiteralEntry {
        int type;
        int offset;
        LiteralValue value;
        double numericValue;
    };

    struct PropertyEntry {
        int nameId;
    };

    struct GlobalEntry {
        int nameId;
    };

    ScriptChunk(const DirectorFile* file,
                id::ChunkId id,
                ScriptChunkType scriptType,
                int behaviorFlags,
                std::vector<Handler> handlers,
                std::vector<LiteralEntry> literals,
                std::vector<PropertyEntry> properties,
                std::vector<GlobalEntry> globals,
                std::vector<std::uint8_t> rawBytecode);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] ScriptChunkType scriptType() const;
    [[nodiscard]] int behaviorFlags() const;
    [[nodiscard]] const std::vector<Handler>& handlers() const;
    [[nodiscard]] const std::vector<LiteralEntry>& literals() const;
    [[nodiscard]] const std::vector<PropertyEntry>& properties() const;
    [[nodiscard]] const std::vector<GlobalEntry>& globals() const;
    [[nodiscard]] const std::vector<std::uint8_t>& rawBytecode() const;
    [[nodiscard]] bool hasProperties() const;
    [[nodiscard]] bool hasGlobals() const;
    [[nodiscard]] ScriptChunkType resolvedScriptType() const;
    [[nodiscard]] std::string scriptName() const;
    [[nodiscard]] std::string displayName() const;
    [[nodiscard]] std::optional<Handler> findHandlerByNameId(int nameId) const;
    [[nodiscard]] const Handler* findHandlerByNameIdPtr(int nameId) const;
    [[nodiscard]] std::string getHandlerName(const Handler& handler) const;
    [[nodiscard]] std::string getHandlerName(const Handler& handler, const ScriptNamesChunk* names) const;
    [[nodiscard]] std::string resolveName(int nameId) const;
    [[nodiscard]] std::string resolveName(int nameId, const ScriptNamesChunk* names) const;
    [[nodiscard]] std::optional<Handler> findHandler(std::string_view name) const;
    [[nodiscard]] std::optional<Handler> findHandler(std::string_view name, const ScriptNamesChunk* names) const;
    [[nodiscard]] const Handler* findHandlerPtr(std::string_view name) const;
    [[nodiscard]] const Handler* findHandlerPtr(std::string_view name, const ScriptNamesChunk* names) const;
    [[nodiscard]] std::vector<std::string> getPropertyNames() const;
    [[nodiscard]] std::vector<std::string> getPropertyNames(const ScriptNamesChunk* names) const;
    [[nodiscard]] std::vector<std::string> getGlobalNames() const;
    [[nodiscard]] std::vector<std::string> getGlobalNames(const ScriptNamesChunk* names) const;

    [[nodiscard]] static ScriptChunk read(const DirectorFile* file,
                                          io::BinaryReader& reader,
                                          id::ChunkId id,
                                          int version,
                                          bool capitalX = false);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    ScriptChunkType scriptType_;
    int behaviorFlags_;
    std::vector<Handler> handlers_;
    std::vector<LiteralEntry> literals_;
    std::vector<PropertyEntry> properties_;
    std::vector<GlobalEntry> globals_;
    std::vector<std::uint8_t> rawBytecode_;
};

} // namespace libreshockwave::chunks
