#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/format/chunk_info.hpp"
#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::format {

class AfterburnerReader {
public:
    AfterburnerReader(io::BinaryReader reader, io::ByteOrder byte_order);

    void parse();
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getChunkData(int resource_id);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getChunkDataByType(const std::string& four_cc);
    [[nodiscard]] std::vector<ChunkInfo> getChunksByType(const std::string& four_cc) const;
    void clearCachedData();

    [[nodiscard]] int directorVersion() const;
    [[nodiscard]] int imapVersion() const;
    [[nodiscard]] const std::string& versionString() const;
    [[nodiscard]] const std::vector<MoaID>& compressionTypes() const;
    [[nodiscard]] int chunkCount() const;
    [[nodiscard]] std::vector<ChunkInfo> chunkInfos() const;
    [[nodiscard]] const ChunkInfo* getChunkInfo(int resource_id) const;

private:
    [[nodiscard]] std::string readFourCCOrdered();
    [[nodiscard]] std::string readFourCCOrdered(io::BinaryReader& reader) const;
    [[nodiscard]] int readVarInt();
    [[nodiscard]] int readVarInt(io::BinaryReader& reader) const;

    void readFverChunk();
    void readFcdrChunk();
    void readAbmpChunk();
    void readIlsChunk();

    io::BinaryReader reader_;
    io::ByteOrder byte_order_;
    int director_version_ = 0;
    int imap_version_ = 0;
    std::string version_string_;
    std::vector<MoaID> compression_types_;
    std::map<int, ChunkInfo> chunk_info_map_;
    std::vector<std::uint8_t> ils_data_;
    int ils_body_offset_ = 0;
    std::map<int, std::vector<std::uint8_t>> cached_chunk_data_;
};

} // namespace libreshockwave::format
