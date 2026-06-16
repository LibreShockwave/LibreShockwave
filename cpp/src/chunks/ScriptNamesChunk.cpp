#include "libreshockwave/chunks/ScriptNamesChunk.hpp"

#include <cctype>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

ScriptNamesChunk::ScriptNamesChunk(const DirectorFile* file, id::ChunkId id, std::vector<std::string> names)
    : file_(file), id_(id), names_(std::move(names)) {}

const DirectorFile* ScriptNamesChunk::file() const { return file_; }
format::ChunkType ScriptNamesChunk::type() const { return format::ChunkType::Lnam; }
id::ChunkId ScriptNamesChunk::id() const { return id_; }
const std::vector<std::string>& ScriptNamesChunk::names() const { return names_; }

std::string ScriptNamesChunk::getName(int index) const {
    if (index >= 0 && index < static_cast<int>(names_.size())) {
        return names_[static_cast<std::size_t>(index)];
    }
    return "<unknown:" + std::to_string(index) + ">";
}

int ScriptNamesChunk::findName(std::string_view value) const {
    for (int index = 0; index < static_cast<int>(names_.size()); ++index) {
        if (equalsIgnoreCase(names_[static_cast<std::size_t>(index)], value)) {
            return index;
        }
    }
    return -1;
}

ScriptNamesChunk ScriptNamesChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);
    (void)reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    const int namesOffset = reader.readU16();
    const int namesCount = reader.readU16();

    std::vector<std::string> names;
    reader.setPosition(static_cast<std::size_t>(namesOffset));
    for (int index = 0; index < namesCount; ++index) {
        const int length = reader.readU8();
        names.push_back(reader.readStringMacRoman(static_cast<std::size_t>(length)));
    }

    return ScriptNamesChunk(file, id, std::move(names));
}

} // namespace libreshockwave::chunks
