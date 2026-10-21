#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/cast/member_type.hpp"

namespace libreshockwave::moviesource {

// A linked asset reference: a fourcc tag and a reverse-lookup key into the
// owning format source (e.g. a chunk id). The owning source resolves the key
// to the underlying data.
struct LinkedAsset {
    std::uint32_t fourcc = 0;
    int chunk_id = 0;
};

// A cast library asset: the cast lib number and its member ids.
struct CastAsset {
    int cast_lib = 0;
    std::vector<int> member_ids;
};

// A cast member asset. Carries the member identity, type, and the raw payload
// needed for lazy decoding. The `chunk_id` is the reverse-lookup key into the
// owning format source.
struct CastMemberAsset {
    int chunk_id = 0;
    int cast_lib = 0;
    int member_num = 0;
    std::string name;
    cast::MemberType type = cast::MemberType::Null;
    int script_id = 0;
    int reg_point_x = 0;
    int reg_point_y = 0;
    // Raw member info and specific-data payloads, for lazy decoding.
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> specific_data;
    // Linked asset references (fourcc + reverse-lookup key).
    std::vector<LinkedAsset> linked;

    [[nodiscard]] bool isBitmap() const { return type == cast::MemberType::Bitmap; }
    [[nodiscard]] bool isScript() const { return type == cast::MemberType::Script; }
    [[nodiscard]] bool isText() const { return type == cast::MemberType::Text; }
    [[nodiscard]] bool isSound() const { return type == cast::MemberType::Sound; }
    [[nodiscard]] bool isShockwave3D() const { return type == cast::MemberType::Shockwave3D; }
};

// A cast list asset: the ordered cast library entries.
struct CastListAsset {
    struct Entry {
        std::string name;
        std::string path;
        int preload_settings = 0;
        int min_member = 0;
        int max_member = 0;
        int member_count = 0;
        int id = 0;
    };
    std::vector<Entry> entries;
};

} // namespace libreshockwave::moviesource
