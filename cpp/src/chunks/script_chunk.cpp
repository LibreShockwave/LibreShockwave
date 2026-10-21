#include "libreshockwave/chunks/script_chunk.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <utility>

#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/format/script_format_utils.hpp"
#include "libreshockwave/moviesource/director_movie_source.hpp"

namespace libreshockwave::chunks {
namespace {

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), original_order_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(original_order_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder original_order_;
};

bool canReadAt(const io::BinaryReader& reader, int offset, std::size_t length) {
    if (offset < 0) {
        return false;
    }
    const auto start = static_cast<std::size_t>(offset);
    return start <= reader.length() && length <= reader.length() - start;
}

std::vector<int> readNameIds(io::BinaryReader& reader, int count, int offset) {
    std::vector<int> ids;
    if (count <= 0 || !canReadAt(reader, offset, static_cast<std::size_t>(count) * 2U)) {
        return ids;
    }

    const auto saved_position = reader.position();
    reader.seek(static_cast<std::size_t>(offset));
    ids.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        ids.push_back(reader.readU16());
    }
    reader.seek(saved_position);
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
    if (raw_opcode >= 0x40) {
        out << " " << argument;
    }
    return out.str();
}

int ScriptChunk::Handler::getInstructionIndex(int offset) const {
    if (const auto found = bytecode_index_map.find(offset); found != bytecode_index_map.end()) {
        return found->second;
    }
    return -1;
}

ScriptChunk::ScriptChunk(const moviesource::MovieSource* file,
                         id::ChunkId id,
                         ScriptChunkType script_type,
                         int behavior_flags,
                         std::vector<Handler> handlers,
                         std::vector<LiteralEntry> literals,
                         std::vector<PropertyEntry> properties,
                         std::vector<GlobalEntry> globals,
                         std::vector<std::uint8_t> raw_bytecode)
    : file_(file),
      id_(id),
      script_type_(script_type),
      behavior_flags_(behavior_flags),
      handlers_(std::move(handlers)),
      literals_(std::move(literals)),
      properties_(std::move(properties)),
      globals_(std::move(globals)),
      raw_bytecode_(std::move(raw_bytecode)) {}

const moviesource::MovieSource* ScriptChunk::file() const { return file_; }
format::ChunkType ScriptChunk::type() const { return format::ChunkType::Lscr; }
id::ChunkId ScriptChunk::id() const { return id_; }
ScriptChunkType ScriptChunk::scriptType() const { return script_type_; }

ScriptChunkType ScriptChunk::resolvedScriptType() const {
    if (file_ != nullptr) {
        return static_cast<const moviesource::DirectorMovieSource*>(file_)->resolvedScriptType(*this);
    }
    return script_type_;
}
int ScriptChunk::behaviorFlags() const { return behavior_flags_; }
const std::vector<ScriptChunk::Handler>& ScriptChunk::handlers() const { return handlers_; }
const std::vector<ScriptChunk::LiteralEntry>& ScriptChunk::literals() const { return literals_; }
const std::vector<ScriptChunk::PropertyEntry>& ScriptChunk::properties() const { return properties_; }
const std::vector<ScriptChunk::GlobalEntry>& ScriptChunk::globals() const { return globals_; }
const std::vector<std::uint8_t>& ScriptChunk::rawBytecode() const { return raw_bytecode_; }
bool ScriptChunk::hasProperties() const { return !properties_.empty(); }
bool ScriptChunk::hasGlobals() const { return !globals_.empty(); }

std::optional<ScriptChunk::Handler> ScriptChunk::findHandlerByNameId(int name_id) const {
    const auto* handler = findHandlerByNameIdPtr(name_id);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

const ScriptChunk::Handler* ScriptChunk::findHandlerByNameIdPtr(int name_id) const {
    for (const auto& handler : handlers_) {
        if (handler.name_id == name_id) {
            return &handler;
        }
    }
    return nullptr;
}

std::string ScriptChunk::getHandlerName(const Handler& handler, const ScriptNamesChunk* names) const {
    if (names) {
        return names->getName(handler.name_id);
    }
    return "handler#" + std::to_string(handler.name_id);
}

std::string ScriptChunk::resolveName(int name_id, const ScriptNamesChunk* names) const {
    if (names) {
        return names->getName(name_id);
    }
    return "#" + std::to_string(name_id);
}

std::optional<ScriptChunk::Handler> ScriptChunk::findHandler(std::string_view name, const ScriptNamesChunk* names) const {
    const auto* handler = findHandlerPtr(name, names);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

const ScriptChunk::Handler* ScriptChunk::findHandlerPtr(std::string_view name, const ScriptNamesChunk* names) const {
    if (!names) {
        return nullptr;
    }
    for (const auto& handler : handlers_) {
        if (equalsIgnoreCase(names->getName(handler.name_id), name)) {
            return &handler;
        }
    }
    return nullptr;
}

std::vector<std::string> ScriptChunk::getPropertyNames(const ScriptNamesChunk* names) const {
    std::vector<std::string> result;
    if (!names) {
        return result;
    }
    result.reserve(properties_.size());
    for (const auto& property : properties_) {
        result.push_back(names->getName(property.name_id));
    }
    return result;
}

std::vector<std::string> ScriptChunk::getGlobalNames(const ScriptNamesChunk* names) const {
    std::vector<std::string> result;
    if (!names) {
        return result;
    }
    result.reserve(globals_.size());
    for (const auto& global : globals_) {
        result.push_back(names->getName(global.name_id));
    }
    return result;
}

ScriptChunk ScriptChunk::read(const moviesource::MovieSource* file,
                              io::BinaryReader& reader,
                              id::ChunkId chunk_id,
                              int version,
                              bool capital_x) {
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    int script_number = 0;
    int behavior_flags = 0;
    int handler_vectors_count = 0;
    int handler_vectors_offset = 0;
    int handler_vectors_size = 0;
    int property_count = 0;
    int properties_offset = 0;
    int global_count = 0;
    int globals_offset = 0;
    int handler_info_count = 0;
    int handlers_offset = 0;
    int literal_count = 0;
    int literals_offset = 0;
    int literal_data_len = 0;
    int literal_data_offset = 0;

    if (canReadAt(reader, 8, 12)) {
        reader.seek(8);
        (void)reader.readI32();
        (void)reader.readI32();
        (void)reader.readU16();
        script_number = reader.readU16();
    }

    if (canReadAt(reader, 38, 4)) {
        reader.seek(38);
        behavior_flags = reader.readI32();
    }

    int script_type_code = behavior_flags & 0x0F;
    if (script_type_code == 0) {
        script_type_code = script_number;
    }

    if (canReadAt(reader, 50, 42)) {
        reader.seek(50);
        handler_vectors_count = reader.readU16();
        handler_vectors_offset = reader.readI32();
        handler_vectors_size = reader.readI32();
        property_count = reader.readU16();
        properties_offset = reader.readI32();
        global_count = reader.readU16();
        globals_offset = reader.readI32();
        handler_info_count = reader.readU16();
        handlers_offset = reader.readI32();
        literal_count = reader.readU16();
        literals_offset = reader.readI32();
        literal_data_len = reader.readI32();
        literal_data_offset = reader.readI32();
    }
    (void)handler_vectors_count;
    (void)handler_vectors_offset;
    (void)handler_vectors_size;
    (void)literal_data_len;

    std::vector<PropertyEntry> properties;
    if (property_count > 0 && canReadAt(reader, properties_offset, static_cast<std::size_t>(property_count) * 2U)) {
        reader.seek(static_cast<std::size_t>(properties_offset));
        properties.reserve(static_cast<std::size_t>(property_count));
        for (int index = 0; index < property_count; ++index) {
            properties.push_back(PropertyEntry{reader.readI16()});
        }
    }

    std::vector<GlobalEntry> globals;
    if (global_count > 0 && canReadAt(reader, globals_offset, static_cast<std::size_t>(global_count) * 2U)) {
        reader.seek(static_cast<std::size_t>(globals_offset));
        globals.reserve(static_cast<std::size_t>(global_count));
        for (int index = 0; index < global_count; ++index) {
            globals.push_back(GlobalEntry{reader.readI16()});
        }
    }

    std::vector<LiteralEntry> literals;
    const int literal_record_len = version < 0x4B1 ? 6 : 8;
    if (literal_count > 0 && canReadAt(reader, literals_offset, static_cast<std::size_t>(literal_count * literal_record_len))) {
        reader.seek(static_cast<std::size_t>(literals_offset));

        struct LiteralInfo {
            int type;
            int offset;
        };
        std::vector<LiteralInfo> literal_info;
        literal_info.reserve(static_cast<std::size_t>(literal_count));
        for (int index = 0; index < literal_count; ++index) {
            const int literal_type = version < 0x4B1 ? reader.readU16() : reader.readI32();
            const int offset = reader.readI32();
            literal_info.push_back(LiteralInfo{literal_type, offset});
        }

        literals.reserve(literal_info.size());
        for (const auto& info : literal_info) {
            LiteralValue value;
            double numeric_value = 0.0;

            if (info.type == 4) {
                value = info.offset;
            } else if (canReadAt(reader, literal_data_offset + info.offset, 4)) {
                reader.seek(static_cast<std::size_t>(literal_data_offset + info.offset));
                const int data_len = reader.readI32();
                if (data_len >= 0 && static_cast<std::size_t>(data_len) <= reader.bytesLeft()) {
                    switch (info.type) {
                        case 1: {
                            std::string text = reader.readStringMacRoman(static_cast<std::size_t>(data_len));
                            if (!text.empty() && text.back() == '\0') {
                                text.pop_back();
                            }
                            value = std::move(text);
                            break;
                        }
                        case 9: {
                            // Literal floats are stored as 8-byte IEEE doubles.
                            if (reader.bytesLeft() >= 8) {
                                const auto bits = static_cast<std::uint64_t>(reader.readI64());
                                numeric_value = std::bit_cast<double>(bits);
                                value = format::floatingToString(numeric_value);
                            }
                            break;
                        }
                        default:
                            value = reader.readBytes(static_cast<std::size_t>(data_len));
                            break;
                    }
                }
            }

            literals.push_back(LiteralEntry{info.type, info.offset, std::move(value), numeric_value});
        }
    }

    std::vector<Handler> handlers;
    const int handler_record_len = capital_x ? 46 : 42;
    if (handler_info_count > 0 && canReadAt(reader, handlers_offset, static_cast<std::size_t>(handler_info_count * handler_record_len))) {
        reader.seek(static_cast<std::size_t>(handlers_offset));
        handlers.reserve(static_cast<std::size_t>(handler_info_count));

        for (int index = 0; index < handler_info_count; ++index) {
            const int name_id = reader.readI16();
            const int handler_vector_pos = reader.readU16();
            const int bytecode_len = reader.readI32();
            const int bytecode_offset = reader.readI32();
            const int arg_count = reader.readU16();
            const int arg_offset = reader.readI32();
            const int local_count = reader.readU16();
            const int local_offset = reader.readI32();
            const int handler_globals_count = reader.readU16();
            const int globals_offset = reader.readI32();
            (void)reader.readI32();
            (void)reader.readU16();
            const int line_count = reader.readU16();
            (void)reader.readI32();
            if (capital_x) {
                (void)reader.readI32();
            }

            const auto saved_position = reader.position();
            auto arg_name_ids = readNameIds(reader, arg_count, arg_offset);
            auto local_name_ids = readNameIds(reader, local_count, local_offset);
            auto global_name_ids = readNameIds(reader, handler_globals_count, globals_offset);
            // Director leaves unused global slots as 0xFFFF. They carry no name
            // and cannot be represented in script text.
            global_name_ids.erase(
                std::remove(global_name_ids.begin(), global_name_ids.end(), 0xFFFF),
                global_name_ids.end());

            std::vector<Instruction> instructions;
            std::unordered_map<int, int> bytecode_index_map;
            if (bytecode_len > 0 && canReadAt(reader, bytecode_offset, static_cast<std::size_t>(bytecode_len))) {
                reader.seek(static_cast<std::size_t>(bytecode_offset));
                const auto bytecode_end = static_cast<std::size_t>(bytecode_offset) + static_cast<std::size_t>(bytecode_len);

                while (reader.position() < bytecode_end) {
                    const int instr_offset = static_cast<int>(reader.position()) - bytecode_offset;
                    const int op = reader.readU8();
                    const auto opcode = lingo::opcodeFromCode(op >= 0x40 ? (0x40 + op % 0x40) : op);
                    int argument = 0;

                    if (op >= 0xC0) {
                        if (reader.position() + 4 > bytecode_end) {
                            break;
                        }
                        argument = reader.readI32();
                    } else if (op >= 0x80) {
                        if (reader.position() + 2 > bytecode_end) {
                            break;
                        }
                        argument = (opcode == lingo::Opcode::PUSH_INT16 || opcode == lingo::Opcode::PUSH_INT8)
                            ? reader.readI16()
                            : reader.readU16();
                    } else if (op >= 0x40) {
                        if (reader.position() + 1 > bytecode_end) {
                            break;
                        }
                        argument = opcode == lingo::Opcode::PUSH_INT8 ? reader.readI8() : reader.readU8();
                    }

                    bytecode_index_map[instr_offset] = static_cast<int>(instructions.size());
                    instructions.push_back(Instruction{instr_offset, opcode, op, argument});
                }
            }
            reader.seek(saved_position);

            handlers.push_back(Handler{name_id,
                                       handler_vector_pos,
                                       bytecode_len,
                                       bytecode_offset,
                                       arg_count,
                                       local_count,
                                       handler_globals_count,
                                       line_count,
                                       std::move(arg_name_ids),
                                       std::move(local_name_ids),
                                       std::move(instructions),
                                       std::move(bytecode_index_map),
                                       std::move(global_name_ids)});
        }
    }

    return ScriptChunk(file,
                       chunk_id,
                       scriptChunkTypeFromCode(script_type_code),
                       behavior_flags,
                       std::move(handlers),
                       std::move(literals),
                       std::move(properties),
                       std::move(globals),
                       {});
}

} // namespace libreshockwave::chunks
