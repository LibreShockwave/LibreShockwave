#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace libreshockwave::util {

[[nodiscard]] std::string getFileName(std::string_view path);
[[nodiscard]] std::string fileExtension(std::string_view path);
[[nodiscard]] std::vector<std::string> getUrlsWithFallbacks(std::string_view url);
[[nodiscard]] std::string getFileNameWithoutExtension(std::string_view path);
[[nodiscard]] std::vector<std::filesystem::path> getLocalFetchPathCandidates(
    const std::filesystem::path& base_path,
    std::string_view url);

} // namespace libreshockwave::util
