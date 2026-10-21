#include "libreshockwave/moviesource/format_registry.hpp"

#include <algorithm>
#include <cctype>
#include <map>

#include "libreshockwave/moviesource/director_movie_source.hpp"
#include "libreshockwave/moviesource/lsw_movie_source.hpp"

namespace libreshockwave::moviesource {

namespace {

std::string normalizeExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

std::map<std::string, MovieSourceFactory>& registry() {
    static std::map<std::string, MovieSourceFactory> registry_;
    return registry_;
}

} // namespace

void FormatRegistry::registerExtension(std::string extension, MovieSourceFactory factory) {
    registry()[normalizeExtension(std::move(extension))] = std::move(factory);
}

const MovieSourceFactory* FormatRegistry::findExtension(const std::string& extension) {
    const auto found = registry().find(normalizeExtension(extension));
    if (found != registry().end()) {
        return &found->second;
    }
    return nullptr;
}

std::vector<std::string> FormatRegistry::registeredExtensions() {
    std::vector<std::string> extensions;
    extensions.reserve(registry().size());
    for (const auto& entry : registry()) {
        extensions.push_back(entry.first);
    }
    return extensions;
}

void registerDirectorFormat() {
    const char* extensions[] = {"dir", "dcr", "dxr", "cct", "cst"};
    for (const char* extension : extensions) {
        FormatRegistry::registerExtension(extension, [](const std::vector<std::uint8_t>& data) {
            auto source = std::make_shared<DirectorMovieSource>();
            source->load(data);
            return source;
        });
    }
}

void registerLswFormat() {
    FormatRegistry::registerExtension("lsw", [](const std::vector<std::uint8_t>& data) {
        auto source = std::make_shared<LswMovieSource>();
        source->load(data);
        return source;
    });
}

} // namespace libreshockwave::moviesource
