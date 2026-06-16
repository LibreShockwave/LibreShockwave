#pragma once

#include <string>

#include "libreshockwave/format/MoaID.hpp"

namespace libreshockwave::format {

struct ChunkInfo {
    int resourceId;
    std::string fourCC;
    int offset;
    int compressedSize;
    int uncompressedSize;
    MoaID compressionType;

    [[nodiscard]] bool isCompressed() const;
    [[nodiscard]] bool isZlibCompressed() const;
    [[nodiscard]] std::string toString() const;
};

inline bool ChunkInfo::isCompressed() const {
    return compressedSize != uncompressedSize && !compressionType.isNull();
}

inline bool ChunkInfo::isZlibCompressed() const {
    return compressionType.isZlib();
}

inline std::string ChunkInfo::toString() const {
    std::string compression = "unknown";
    if (compressionType.isZlib()) {
        compression = "zlib";
    } else if (compressionType.isSound()) {
        compression = "sound";
    } else if (compressionType.isNull()) {
        compression = "none";
    }

    return "ChunkInfo[id=" + std::to_string(resourceId) +
           ", type=" + fourCC +
           ", offset=" + std::to_string(offset) +
           ", size=" + std::to_string(compressedSize) +
           "->" + std::to_string(uncompressedSize) +
           ", compression=" + compression + "]";
}

} // namespace libreshockwave::format
