#include "libreshockwave/chunks/ScriptChunk.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"

namespace libreshockwave::chunks {
namespace {

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), originalOrder_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(originalOrder_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder originalOrder_;
};

bool canReadAt(const io::BinaryReader& reader, int offset, std::size_t length) {
    if (offset < 0) {
        return false;
    }
    const auto start = static_cast<std::size_t>(offset);
    return start <= reader.length() && length <= reader.length() - start;
}

std::string floatingToString(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

std::vector<int> readNameIds(io::BinaryReader& reader, int count, int offset) {
    std::vector<int> ids;
    if (count <= 0 || !canReadAt(reader, offset, static_cast<std::size_t>(count) * 2U)) {
        return ids;
    }

    const auto savedPosition = reader.position();
    reader.seek(static_cast<std::size_t>(offset));
    ids.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        ids.push_back(reader.readU16());
    }
    reader.seek(savedPosition);
    return ids;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

ScriptChunkType scriptChunkTypeForCastMemberScriptType(CastMemberScriptType type) {
    switch (type) {
        case CastMemberScriptType::Score: return ScriptChunkType::Score;
        case CastMemberScriptType::Behavior: return ScriptChunkType::Behavior;
        case CastMemberScriptType::MovieScript: return ScriptChunkType::MovieScript;
        case CastMemberScriptType::Parent: return ScriptChunkType::Parent;
        case CastMemberScriptType::Unknown: return ScriptChunkType::Unknown;
    }
    return ScriptChunkType::Unknown;
}

std::shared_ptr<ScriptChunk> findScriptInFile(const ScriptChunk& script) {
    const auto* file = script.file();
    if (!file) {
        return nullptr;
    }

    for (const auto& candidate : file->scripts()) {
        if (candidate && candidate.get() == &script) {
            return candidate;
        }
    }
    for (const auto& candidate : file->scripts()) {
        if (candidate && candidate->id().value() == script.id().value()) {
            return candidate;
        }
    }
    return nullptr;
}

std::shared_ptr<ScriptNamesChunk> scriptNamesFromFile(const ScriptChunk& script) {
    auto* file = const_cast<DirectorFile*>(script.file());
    if (!file) {
        return nullptr;
    }
    return file->getScriptNamesForScript(findScriptInFile(script));
}

} // namespace

ScriptChunkType scriptChunkTypeFromCode(int code) {
    switch (code) {
        case 1: return ScriptChunkType::Score;
        case 2: return ScriptChunkType::Behavior;
        case 3: return ScriptChunkType::MovieScript;
        case 7: return ScriptChunkType::Parent;
        default: return ScriptChunkType::Unknown;
    }
}

std::string ScriptChunk::Instruction::toString() const {
    std::ostringstream out;
    out << "[" << offset << "] " << lingo::mnemonic(opcode);
    if (rawOpcode >= 0x40) {
        out << " " << argument;
    }
    return out.str();
}

int ScriptChunk::Handler::getInstructionIndex(int offset) const {
    if (const auto found = bytecodeIndexMap.find(offset); found != bytecodeIndexMap.end()) {
        return found->second;
    }
    return -1;
}

ScriptChunk::ScriptChunk(const DirectorFile* file,
                         id::ChunkId id,
                         ScriptChunkType scriptType,
                         int behaviorFlags,
                         std::vector<Handler> handlers,
                         std::vector<LiteralEntry> literals,
                         std::vector<PropertyEntry> properties,
                         std::vector<GlobalEntry> globals,
                         std::vector<std::uint8_t> rawBytecode)
    : file_(file),
      id_(id),
      scriptType_(scriptType),
      behaviorFlags_(behaviorFlags),
      handlers_(std::move(handlers)),
      literals_(std::move(literals)),
      properties_(std::move(properties)),
      globals_(std::move(globals)),
      rawBytecode_(std::move(rawBytecode)) {}

const DirectorFile* ScriptChunk::file() const { return file_; }
format::ChunkType ScriptChunk::type() const { return format::ChunkType::Lscr; }
id::ChunkId ScriptChunk::id() const { return id_; }
ScriptChunkType ScriptChunk::scriptType() const { return scriptType_; }
int ScriptChunk::behaviorFlags() const { return behaviorFlags_; }
const std::vector<ScriptChunk::Handler>& ScriptChunk::handlers() const { return handlers_; }
const std::vector<ScriptChunk::LiteralEntry>& ScriptChunk::literals() const { return literals_; }
const std::vector<ScriptChunk::PropertyEntry>& ScriptChunk::properties() const { return properties_; }
const std::vector<ScriptChunk::GlobalEntry>& ScriptChunk::globals() const { return globals_; }
const std::vector<std::uint8_t>& ScriptChunk::rawBytecode() const { return rawBytecode_; }
bool ScriptChunk::hasProperties() const { return !properties_.empty(); }
bool ScriptChunk::hasGlobals() const { return !globals_.empty(); }

ScriptChunkType ScriptChunk::resolvedScriptType() const {
    auto* file = const_cast<DirectorFile*>(file_);
    const auto script = findScriptInFile(*this);
    if (file && script) {
        if (const auto castMemberType = file->getScriptType(script); castMemberType.has_value()) {
            return scriptChunkTypeForCastMemberScriptType(castMemberType.value());
        }
    }
    return scriptType_;
}

std::string ScriptChunk::scriptName() const {
    auto* file = const_cast<DirectorFile*>(file_);
    const auto script = findScriptInFile(*this);
    return file && script ? file->getScriptName(script) : "";
}

std::string ScriptChunk::displayName() const {
    const auto name = scriptName();
    const auto typeName = format::getScriptTypeName(resolvedScriptType());
    if (!name.empty()) {
        return "\"" + name + "\" (" + typeName + ")";
    }
    return typeName + " #" + std::to_string(id_.value());
}

std::optional<ScriptChunk::Handler> ScriptChunk::findHandlerByNameId(int nameId) const {
    const auto* handler = findHandlerByNameIdPtr(nameId);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

const ScriptChunk::Handler* ScriptChunk::findHandlerByNameIdPtr(int nameId) const {
    for (const auto& handler : handlers_) {
        if (handler.nameId == nameId) {
            return &handler;
        }
    }
    return nullptr;
}

std::string ScriptChunk::getHandlerName(const Handler& handler) const {
    return getHandlerName(handler, scriptNamesFromFile(*this).get());
}

std::string ScriptChunk::getHandlerName(const Handler& handler, const ScriptNamesChunk* names) const {
    if (names) {
        return names->getName(handler.nameId);
    }
    return "handler#" + std::to_string(handler.nameId);
}

std::string ScriptChunk::resolveName(int nameId) const {
    return resolveName(nameId, scriptNamesFromFile(*this).get());
}

std::string ScriptChunk::resolveName(int nameId, const ScriptNamesChunk* names) const {
    if (names) {
        return names->getName(nameId);
    }
    return "#" + std::to_string(nameId);
}

std::optional<ScriptChunk::Handler> ScriptChunk::findHandler(std::string_view name) const {
    return findHandler(name, scriptNamesFromFile(*this).get());
}

std::optional<ScriptChunk::Handler> ScriptChunk::findHandler(std::string_view name, const ScriptNamesChunk* names) const {
    const auto* handler = findHandlerPtr(name, names);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

const ScriptChunk::Handler* ScriptChunk::findHandlerPtr(std::string_view name) const {
    return findHandlerPtr(name, scriptNamesFromFile(*this).get());
}

const ScriptChunk::Handler* ScriptChunk::findHandlerPtr(std::string_view name, const ScriptNamesChunk* names) const {
    if (!names) {
        return nullptr;
    }
    for (const auto& handler : handlers_) {
        if (equalsIgnoreCase(names->getName(handler.nameId), name)) {
            return &handler;
        }
    }
    return nullptr;
}

std::vector<std::string> ScriptChunk::getPropertyNames() const {
    return getPropertyNames(scriptNamesFromFile(*this).get());
}

std::vector<std::string> ScriptChunk::getPropertyNames(const ScriptNamesChunk* names) const {
    std::vector<std::string> result;
    if (!names) {
        return result;
    }
    result.reserve(properties_.size());
    for (const auto& property : properties_) {
        result.push_back(names->getName(property.nameId));
    }
    return result;
}

std::vector<std::string> ScriptChunk::getGlobalNames() const {
    return getGlobalNames(scriptNamesFromFile(*this).get());
}

std::vector<std::string> ScriptChunk::getGlobalNames(const ScriptNamesChunk* names) const {
    std::vector<std::string> result;
    if (!names) {
        return result;
    }
    result.reserve(globals_.size());
    for (const auto& global : globals_) {
        result.push_back(names->getName(global.nameId));
    }
    return result;
}

ScriptChunk ScriptChunk::read(const DirectorFile* file,
                              io::BinaryReader& reader,
                              id::ChunkId chunkId,
                              int version,
                              bool capitalX) {
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    int scriptNumber = 0;
    int behaviorFlags = 0;
    int handlerVectorsCount = 0;
    int handlerVectorsOffset = 0;
    int handlerVectorsSize = 0;
    int propertyCount = 0;
    int propertiesOffset = 0;
    int globalCount = 0;
    int globalsOffset = 0;
    int handlerInfoCount = 0;
    int handlersOffset = 0;
    int literalCount = 0;
    int literalsOffset = 0;
    int literalDataLen = 0;
    int literalDataOffset = 0;

    if (canReadAt(reader, 8, 12)) {
        reader.seek(8);
        (void)reader.readI32();
        (void)reader.readI32();
        (void)reader.readU16();
        scriptNumber = reader.readU16();
    }

    if (canReadAt(reader, 38, 4)) {
        reader.seek(38);
        behaviorFlags = reader.readI32();
    }

    int scriptTypeCode = behaviorFlags & 0x0F;
    if (scriptTypeCode == 0) {
        scriptTypeCode = scriptNumber;
    }

    if (canReadAt(reader, 50, 42)) {
        reader.seek(50);
        handlerVectorsCount = reader.readU16();
        handlerVectorsOffset = reader.readI32();
        handlerVectorsSize = reader.readI32();
        propertyCount = reader.readU16();
        propertiesOffset = reader.readI32();
        globalCount = reader.readU16();
        globalsOffset = reader.readI32();
        handlerInfoCount = reader.readU16();
        handlersOffset = reader.readI32();
        literalCount = reader.readU16();
        literalsOffset = reader.readI32();
        literalDataLen = reader.readI32();
        literalDataOffset = reader.readI32();
    }
    (void)handlerVectorsCount;
    (void)handlerVectorsOffset;
    (void)handlerVectorsSize;
    (void)literalDataLen;

    std::vector<PropertyEntry> properties;
    if (propertyCount > 0 && canReadAt(reader, propertiesOffset, static_cast<std::size_t>(propertyCount) * 2U)) {
        reader.seek(static_cast<std::size_t>(propertiesOffset));
        properties.reserve(static_cast<std::size_t>(propertyCount));
        for (int index = 0; index < propertyCount; ++index) {
            properties.push_back(PropertyEntry{reader.readI16()});
        }
    }

    std::vector<GlobalEntry> globals;
    if (globalCount > 0 && canReadAt(reader, globalsOffset, static_cast<std::size_t>(globalCount) * 2U)) {
        reader.seek(static_cast<std::size_t>(globalsOffset));
        globals.reserve(static_cast<std::size_t>(globalCount));
        for (int index = 0; index < globalCount; ++index) {
            globals.push_back(GlobalEntry{reader.readI16()});
        }
    }

    std::vector<LiteralEntry> literals;
    const int literalRecordLen = version < 0x4B1 ? 6 : 8;
    if (literalCount > 0 && canReadAt(reader, literalsOffset, static_cast<std::size_t>(literalCount * literalRecordLen))) {
        reader.seek(static_cast<std::size_t>(literalsOffset));

        struct LiteralInfo {
            int type;
            int offset;
        };
        std::vector<LiteralInfo> literalInfo;
        literalInfo.reserve(static_cast<std::size_t>(literalCount));
        for (int index = 0; index < literalCount; ++index) {
            const int literalType = version < 0x4B1 ? reader.readU16() : reader.readI32();
            const int offset = reader.readI32();
            literalInfo.push_back(LiteralInfo{literalType, offset});
        }

        literals.reserve(literalInfo.size());
        for (const auto& info : literalInfo) {
            LiteralValue value;
            double numericValue = 0.0;

            if (info.type == 4) {
                value = info.offset;
            } else if (canReadAt(reader, literalDataOffset + info.offset, 4)) {
                reader.seek(static_cast<std::size_t>(literalDataOffset + info.offset));
                const int dataLen = reader.readI32();
                if (dataLen >= 0 && static_cast<std::size_t>(dataLen) <= reader.bytesLeft()) {
                    switch (info.type) {
                        case 1: {
                            std::string text = reader.readStringMacRoman(static_cast<std::size_t>(dataLen));
                            if (!text.empty() && text.back() == '\0') {
                                text.pop_back();
                            }
                            value = std::move(text);
                            break;
                        }
                        case 9: {
                            if (reader.bytesLeft() >= 4) {
                                const auto bits = static_cast<std::uint32_t>(reader.readI32());
                                numericValue = static_cast<double>(std::bit_cast<float>(bits));
                                value = floatingToString(numericValue);
                            }
                            break;
                        }
                        default:
                            value = reader.readBytes(static_cast<std::size_t>(dataLen));
                            break;
                    }
                }
            }

            literals.push_back(LiteralEntry{info.type, info.offset, std::move(value), numericValue});
        }
    }

    std::vector<Handler> handlers;
    const int handlerRecordLen = capitalX ? 46 : 42;
    if (handlerInfoCount > 0 && canReadAt(reader, handlersOffset, static_cast<std::size_t>(handlerInfoCount * handlerRecordLen))) {
        reader.seek(static_cast<std::size_t>(handlersOffset));
        handlers.reserve(static_cast<std::size_t>(handlerInfoCount));

        for (int index = 0; index < handlerInfoCount; ++index) {
            const int nameId = reader.readI16();
            const int handlerVectorPos = reader.readU16();
            const int bytecodeLen = reader.readI32();
            const int bytecodeOffset = reader.readI32();
            const int argCount = reader.readU16();
            const int argOffset = reader.readI32();
            const int localCount = reader.readU16();
            const int localOffset = reader.readI32();
            const int handlerGlobalsCount = reader.readU16();
            (void)reader.readI32();
            (void)reader.readI32();
            (void)reader.readU16();
            const int lineCount = reader.readU16();
            (void)reader.readI32();
            if (capitalX) {
                (void)reader.readI32();
            }

            const auto savedPosition = reader.position();
            auto argNameIds = readNameIds(reader, argCount, argOffset);
            auto localNameIds = readNameIds(reader, localCount, localOffset);

            std::vector<Instruction> instructions;
            std::unordered_map<int, int> bytecodeIndexMap;
            if (bytecodeLen > 0 && canReadAt(reader, bytecodeOffset, static_cast<std::size_t>(bytecodeLen))) {
                reader.seek(static_cast<std::size_t>(bytecodeOffset));
                const auto bytecodeEnd = static_cast<std::size_t>(bytecodeOffset) + static_cast<std::size_t>(bytecodeLen);

                while (reader.position() < bytecodeEnd) {
                    const int instrOffset = static_cast<int>(reader.position()) - bytecodeOffset;
                    const int op = reader.readU8();
                    const auto opcode = lingo::opcodeFromCode(op >= 0x40 ? (0x40 + op % 0x40) : op);
                    int argument = 0;

                    if (op >= 0xC0) {
                        if (reader.position() + 4 > bytecodeEnd) {
                            break;
                        }
                        argument = reader.readI32();
                    } else if (op >= 0x80) {
                        if (reader.position() + 2 > bytecodeEnd) {
                            break;
                        }
                        argument = (opcode == lingo::Opcode::PUSH_INT16 || opcode == lingo::Opcode::PUSH_INT8)
                            ? reader.readI16()
                            : reader.readU16();
                    } else if (op >= 0x40) {
                        if (reader.position() + 1 > bytecodeEnd) {
                            break;
                        }
                        argument = opcode == lingo::Opcode::PUSH_INT8 ? reader.readI8() : reader.readU8();
                    }

                    bytecodeIndexMap[instrOffset] = static_cast<int>(instructions.size());
                    instructions.push_back(Instruction{instrOffset, opcode, op, argument});
                }
            }
            reader.seek(savedPosition);

            handlers.push_back(Handler{nameId,
                                       handlerVectorPos,
                                       bytecodeLen,
                                       bytecodeOffset,
                                       argCount,
                                       localCount,
                                       handlerGlobalsCount,
                                       lineCount,
                                       std::move(argNameIds),
                                       std::move(localNameIds),
                                       std::move(instructions),
                                       std::move(bytecodeIndexMap)});
        }
    }

    return ScriptChunk(file,
                       chunkId,
                       scriptChunkTypeFromCode(scriptTypeCode),
                       behaviorFlags,
                       std::move(handlers),
                       std::move(literals),
                       std::move(properties),
                       std::move(globals),
                       {});
}

} // namespace libreshockwave::chunks
