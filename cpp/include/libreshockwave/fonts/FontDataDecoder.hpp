#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::fonts {

class FontDataDecoder {
public:
    [[nodiscard]] static std::vector<std::uint8_t> decode(const std::vector<std::string>& chunks,
                                                          std::size_t compressedLength,
                                                          std::size_t uncompressedLength);
};

} // namespace libreshockwave::fonts
