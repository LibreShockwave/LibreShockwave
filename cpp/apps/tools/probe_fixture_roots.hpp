#pragma once

#include <filesystem>
#include <vector>

namespace libreshockwave::tools {

inline std::vector<std::filesystem::path> defaultFixtureRoots() {
    std::error_code ec;
    const std::filesystem::path requested_root("/var/html");
    if (std::filesystem::exists(requested_root, ec)) {
        return {requested_root};
    }

    const std::filesystem::path mapped_root("/var/www/html");
    if (std::filesystem::exists(mapped_root, ec)) {
        return {mapped_root};
    }
    return {requested_root};
}

} // namespace libreshockwave::tools
