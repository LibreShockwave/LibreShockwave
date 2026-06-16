#include "libreshockwave/util/FileUtil.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace libreshockwave::util {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string toLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::string replacePercent20(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        if (index + 3 <= value.size() && value.substr(index, 3) == "%20") {
            decoded.push_back(' ');
            index += 3;
        } else {
            decoded.push_back(value[index]);
            ++index;
        }
    }
    return decoded;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

void addLocalFetchCandidates(std::vector<std::filesystem::path>& candidates,
                             const std::filesystem::path& directory,
                             const std::string& fileName) {
    if (directory.empty() || fileName.empty()) {
        return;
    }

    candidates.push_back(directory / fileName);

    const std::string stem = getFileNameWithoutExtension(fileName);
    if (!stem.empty()) {
        candidates.push_back(directory / stem / fileName);
    }
}

} // namespace

std::string getFileName(std::string_view path) {
    if (path.empty()) {
        return "";
    }

    if (startsWith(path, "http://") || startsWith(path, "https://")) {
        const auto schemeEnd = path.find("://");
        const auto pathStart = schemeEnd == std::string_view::npos
            ? std::string_view::npos
            : path.find('/', schemeEnd + 3);
        if (pathStart != std::string_view::npos) {
            auto uriPath = path.substr(pathStart);
            if (const auto query = uriPath.find_first_of("?#"); query != std::string_view::npos) {
                uriPath = uriPath.substr(0, query);
            }
            if (!uriPath.empty()) {
                const auto lastSlash = uriPath.find_last_of('/');
                return std::string(lastSlash == std::string_view::npos ? uriPath : uriPath.substr(lastSlash + 1));
            }
        }
    }

    const auto decoded = replacePercent20(path);
    const auto separator = decoded.find_last_of("\\/:");
    return separator == std::string::npos ? decoded : decoded.substr(separator + 1);
}

std::vector<std::string> getUrlsWithFallbacks(std::string_view url) {
    const std::string original(url);
    const auto lower = toLower(url);
    if (endsWith(lower, ".cst") || endsWith(lower, ".cct")) {
        const auto base = original.substr(0, original.size() - 4);
        return {base + ".cct", base + ".cst"};
    }
    if (endsWith(lower, ".dcr") || endsWith(lower, ".dxr") || endsWith(lower, ".dir")) {
        const auto base = original.substr(0, original.size() - 4);
        return {original, base + ".dcr", base + ".dxr", base + ".dir"};
    }

    const auto fileName = getFileName(url);
    if (fileName.find('.') == std::string::npos) {
        return {original + ".cct", original + ".cst"};
    }
    return {original};
}

std::string getFileNameWithoutExtension(std::string_view path) {
    if (path.empty()) {
        return "";
    }

    auto fileName = getFileName(path);
    const auto lastDot = fileName.find_last_of('.');
    if (lastDot == std::string::npos || lastDot == 0) {
        return fileName;
    }
    return fileName.substr(0, lastDot);
}

std::vector<std::filesystem::path> getLocalFetchPathCandidates(const std::filesystem::path& basePath,
                                                               std::string_view url) {
    std::filesystem::path directory = basePath;
    if (std::filesystem::is_regular_file(directory)) {
        directory = directory.parent_path();
    }

    const std::string fileName = getFileName(url);
    std::vector<std::filesystem::path> candidates;
    addLocalFetchCandidates(candidates, directory, fileName);
    addLocalFetchCandidates(candidates, directory.parent_path(), fileName);
    return candidates;
}

} // namespace libreshockwave::util
