#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libreshockwave::moviesource {

// A read-only ZIP archive parsed from memory. It is the container for the
// .lsw movie format. The archive supports stored and deflated entries and does
// no I/O of its own, so it works in native and WASM builds.
//
// The central directory is parsed once. Entry payloads are decompressed on
// demand by extract(). Malformed input is rejected: parse() returns
// std::nullopt, and extract() returns std::nullopt for a bad entry.
class ZipArchive {
public:
    struct Entry {
        std::string name;
        std::uint32_t crc32 = 0;
        std::uint32_t compressed_size = 0;
        std::uint32_t uncompressed_size = 0;
        std::uint16_t method = 0;
        std::size_t local_header_offset = 0;

        [[nodiscard]] bool isStored() const { return method == 0; }
        [[nodiscard]] bool isDeflated() const { return method == 8; }
    };

    // Parse the central directory of a ZIP archive held in memory.
    [[nodiscard]] static std::optional<ZipArchive> parse(std::vector<std::uint8_t> data);

    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] const Entry* find(std::string_view name) const;
    // Decompress one entry. Returns std::nullopt on a malformed entry, a
    // truncated payload, or a compression method without decoder support.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> extract(const Entry& entry) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> extract(std::string_view name) const;

private:
    std::vector<std::uint8_t> data_;
    std::vector<Entry> entries_;
};

} // namespace libreshockwave::moviesource
