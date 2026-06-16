#include "libreshockwave/fonts/FontDataDecoder.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace libreshockwave::fonts {
namespace {

int base64Value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    if (value == '=') {
        return -2;
    }
    return -1;
}

std::vector<std::uint8_t> decodeBase64(std::string_view encoded) {
    if ((encoded.size() % 4) != 0) {
        throw std::invalid_argument("Base64 font chunk length must be a multiple of 4");
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve((encoded.size() / 4) * 3);

    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        std::array<int, 4> values{};
        int padding = 0;
        bool seenPadding = false;

        for (std::size_t index = 0; index < values.size(); ++index) {
            const int value = base64Value(encoded[offset + index]);
            if (value == -1) {
                throw std::invalid_argument("Invalid Base64 character in font chunk");
            }
            if (value == -2) {
                if (index < 2) {
                    throw std::invalid_argument("Invalid Base64 padding in font chunk");
                }
                seenPadding = true;
                ++padding;
                values[index] = 0;
            } else {
                if (seenPadding) {
                    throw std::invalid_argument("Invalid Base64 data after padding in font chunk");
                }
                values[index] = value;
            }
        }

        if (padding > 0 && offset + 4 != encoded.size()) {
            throw std::invalid_argument("Base64 padding is only valid at the end of a font chunk");
        }
        if (padding > 2) {
            throw std::invalid_argument("Invalid Base64 padding count in font chunk");
        }

        decoded.push_back(static_cast<std::uint8_t>((values[0] << 2) | (values[1] >> 4)));
        if (padding < 2) {
            decoded.push_back(static_cast<std::uint8_t>(((values[1] & 0x0F) << 4) | (values[2] >> 2)));
        }
        if (padding < 1) {
            decoded.push_back(static_cast<std::uint8_t>(((values[2] & 0x03) << 6) | values[3]));
        }
    }

    return decoded;
}

std::vector<std::uint8_t> decodeBase64Chunks(const std::vector<std::string>& chunks,
                                             std::size_t compressedLength) {
    std::vector<std::uint8_t> compressed(compressedLength, 0);
    std::size_t position = 0;

    for (const auto& chunk : chunks) {
        auto decoded = decodeBase64(chunk);
        if (decoded.size() > compressed.size() - position) {
            throw std::out_of_range("Decoded font chunk exceeds compressed length");
        }
        std::copy(decoded.begin(), decoded.end(), compressed.begin() + static_cast<std::ptrdiff_t>(position));
        position += decoded.size();
    }

    return compressed;
}

std::vector<std::uint8_t> inflateFontData(const std::vector<std::uint8_t>& compressed,
                                          std::size_t uncompressedLength) {
#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    if (inflateInit(&stream) != Z_OK) {
        return {};
    }

    std::vector<std::uint8_t> output(uncompressedLength, 0);
    int zeroCount = 0;
    int status = Z_OK;

    while (status != Z_STREAM_END && stream.total_out < output.size()) {
        stream.next_out = reinterpret_cast<Bytef*>(output.data() + stream.total_out);
        stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);

        const auto before = stream.total_out;
        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_DATA_ERROR || status == Z_MEM_ERROR || status == Z_STREAM_ERROR) {
            inflateEnd(&stream);
            return {};
        }

        const auto produced = stream.total_out - before;
        if (produced == 0) {
            if (stream.avail_in == 0 || ++zeroCount > 3) {
                break;
            }
            continue;
        }
        zeroCount = 0;
    }

    const auto outputSize = static_cast<std::size_t>(stream.total_out);
    inflateEnd(&stream);

    if (outputSize == output.size()) {
        return output;
    }
    output.resize(outputSize);
    return output;
#else
    (void)compressed;
    (void)uncompressedLength;
    return {};
#endif
}

} // namespace

std::vector<std::uint8_t> FontDataDecoder::decode(const std::vector<std::string>& chunks,
                                                  std::size_t compressedLength,
                                                  std::size_t uncompressedLength) {
    const auto compressed = decodeBase64Chunks(chunks, compressedLength);
    auto data = inflateFontData(compressed, uncompressedLength);
    return data.size() == uncompressedLength ? data : std::vector<std::uint8_t>{};
}

} // namespace libreshockwave::fonts
