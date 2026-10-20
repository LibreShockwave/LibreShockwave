#include "libreshockwave/util/file_util.hpp"

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
                             const std::string& file_name) {
    if (directory.empty() || file_name.empty()) {
        return;
    }

    candidates.push_back(directory / file_name);

    const std::string stem = getFileNameWithoutExtension(file_name);
    if (!stem.empty()) {
        candidates.push_back(directory / stem / file_name);
    }
}

} // namespace

std::string getFileName(std::string_view path) {
    if (path.empty()) {
        return "";
    }

    if (startsWith(path, "http://") || startsWith(path, "https://")) {
        const auto scheme_end = path.find("://");
        const auto path_start = scheme_end == std::string_view::npos
            ? std::string_view::npos
            : path.find('/', scheme_end + 3);
        if (path_start != std::string_view::npos) {
            auto uri_path = path.substr(path_start);
            // Director uses `movie.dcr?/cast.cct` for a cast that is served
            // beside the movie.  The part after `?/` is a virtual path, not
            // an HTTP query, and must be treated as the requested filename
            // by cache and cast-library matching code as well as by HTTP.
            if (const auto virtual_path = uri_path.find("?/");
                virtual_path != std::string_view::npos) {
                uri_path = uri_path.substr(virtual_path + 2);
            } else if (const auto query = uri_path.find_first_of("?#");
                       query != std::string_view::npos) {
                uri_path = uri_path.substr(0, query);
            }
            if (!uri_path.empty()) {
                const auto last_slash = uri_path.find_last_of('/');
                return std::string(last_slash == std::string_view::npos ? uri_path : uri_path.substr(last_slash + 1));
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

    const auto file_name = getFileName(url);
    if (file_name.find('.') == std::string::npos) {
        return {original + ".cct", original + ".cst"};
    }
    return {original};
}

std::string getFileNameWithoutExtension(std::string_view path) {
    if (path.empty()) {
        return "";
    }

    auto file_name = getFileName(path);
    const auto last_dot = file_name.find_last_of('.');
    if (last_dot == std::string::npos || last_dot == 0) {
        return file_name;
    }
    return file_name.substr(0, last_dot);
}

std::vector<std::filesystem::path> getLocalFetchPathCandidates(const std::filesystem::path& base_path,
                                                               std::string_view url) {
    std::filesystem::path directory = base_path;
    if (std::filesystem::is_regular_file(directory)) {
        directory = directory.parent_path();
    }

    const std::string file_name = getFileName(url);
    std::vector<std::filesystem::path> candidates;
    addLocalFetchCandidates(candidates, directory, file_name);
    addLocalFetchCandidates(candidates, directory.parent_path(), file_name);
    return candidates;
}

} // namespace libreshockwave::util
