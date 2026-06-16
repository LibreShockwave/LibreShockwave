#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/format/ChunkInfo.hpp"
#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::format {

class AfterburnerReader {
public:
    AfterburnerReader(io::BinaryReader reader, io::ByteOrder byteOrder);

    void parse();
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getChunkData(int resourceId);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getChunkDataByType(const std::string& fourCC);
    [[nodiscard]] std::vector<ChunkInfo> getChunksByType(const std::string& fourCC) const;
    void clearCachedData();

    [[nodiscard]] int directorVersion() const;
    [[nodiscard]] int imapVersion() const;
    [[nodiscard]] const std::string& versionString() const;
    [[nodiscard]] const std::vector<MoaID>& compressionTypes() const;
    [[nodiscard]] int chunkCount() const;
    [[nodiscard]] std::vector<ChunkInfo> chunkInfos() const;
    [[nodiscard]] const ChunkInfo* getChunkInfo(int resourceId) const;

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
    io::ByteOrder byteOrder_;
    int directorVersion_ = 0;
    int imapVersion_ = 0;
    std::string versionString_;
    std::vector<MoaID> compressionTypes_;
    std::map<int, ChunkInfo> chunkInfoMap_;
    std::vector<std::uint8_t> ilsData_;
    int ilsBodyOffset_ = 0;
    std::map<int, std::vector<std::uint8_t>> cachedChunkData_;
};

} // namespace libreshockwave::format
