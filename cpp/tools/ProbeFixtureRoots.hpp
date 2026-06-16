#pragma once

#include <filesystem>
#include <vector>

namespace libreshockwave::tools {

inline std::vector<std::filesystem::path> defaultFixtureRoots() {
    std::error_code ec;
    const std::filesystem::path requestedRoot("/var/html");
    if (std::filesystem::exists(requestedRoot, ec)) {
        return {requestedRoot};
    }

    const std::filesystem::path mappedRoot("/var/www/html");
    if (std::filesystem::exists(mappedRoot, ec)) {
        return {mappedRoot};
    }
    return {requestedRoot};
}

} // namespace libreshockwave::tools
