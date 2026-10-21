#include "libreshockwave/moviesource/movie_source.hpp"

#include "libreshockwave/moviesource/format_registry.hpp"
#include "libreshockwave/util/file_util.hpp"

namespace libreshockwave::moviesource {

std::shared_ptr<MovieSource> MovieSource::load(const std::string& name,
                                                const std::vector<std::uint8_t>& data) {
    static bool formats_registered = [] {
        registerDirectorFormat();
        registerLswFormat();
        return true;
    }();
    (void)formats_registered;
    const std::string extension = util::fileExtension(name);
    if (extension.empty()) {
        throw MovieSourceLoadError("No file extension for movie source: " + name);
    }
    const auto* factory = FormatRegistry::findExtension(extension);
    if (factory == nullptr) {
        throw MovieSourceLoadError("No registered parser for extension: ." + extension);
    }
    return (*factory)(data);
}

} // namespace libreshockwave::moviesource
