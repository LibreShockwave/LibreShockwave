#pragma once

#include <string>

#include "libreshockwave/format/moa_id.hpp"

namespace libreshockwave::format {

struct ChunkInfo {
    int resource_id;
    std::string four_cc;
    int offset;
    int compressed_size;
    int uncompressed_size;
    MoaID compression_type;

    [[nodiscard]] bool isCompressed() const;
    [[nodiscard]] bool isZlibCompressed() const;
    [[nodiscard]] std::string toString() const;
};

inline bool ChunkInfo::isCompressed() const {
    return compressed_size != uncompressed_size && !compression_type.isNull();
}

inline bool ChunkInfo::isZlibCompressed() const {
    return compression_type.isZlib();
}

inline std::string ChunkInfo::toString() const {
    std::string compression = "unknown";
    if (compression_type.isZlib()) {
        compression = "zlib";
    } else if (compression_type.isSound()) {
        compression = "sound";
    } else if (compression_type.isNull()) {
        compression = "none";
    }

    return "ChunkInfo[id=" + std::to_string(resource_id) +
           ", type=" + four_cc +
           ", offset=" + std::to_string(offset) +
           ", size=" + std::to_string(compressed_size) +
           "->" + std::to_string(uncompressed_size) +
           ", compression=" + compression + "]";
}

} // namespace libreshockwave::format
