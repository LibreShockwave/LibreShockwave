#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace libreshockwave::moviesource {

class MovieSource;

// A factory that builds a MovieSource from raw bytes.
using MovieSourceFactory =
    std::function<std::shared_ptr<MovieSource>(const std::vector<std::uint8_t>&)>;

// The format registry maps a file extension (lowercase, no dot) to a
// MovieSourceFactory. Registration is explicit: a caller registers the Director
// parser (or any future format parser) before calling MovieSource::load.
//
// There are no hidden globals. The registry is a function-local static that is
// populated only through the explicit registerExtension call.
class FormatRegistry {
public:
    // Register a factory for an extension. The extension is normalized to
    // lowercase without a leading dot. Re-registering an extension replaces the
    // existing factory.
    static void registerExtension(std::string extension, MovieSourceFactory factory);

    // Look up the factory for an extension. Returns nullptr if unregistered.
    [[nodiscard]] static const MovieSourceFactory* findExtension(const std::string& extension);

    // The set of registered extensions (normalized). For diagnostics.
    [[nodiscard]] static std::vector<std::string> registeredExtensions();
};

// Register the built-in Director format parser (.dir, .dcr, .dxr, .cct, .cst).
// Idempotent. Called explicitly by the library entry points and the debugger.
void registerDirectorFormat();

// Register the .lsw ZIP format parser. Idempotent. Called by MovieSource::load
// so both native and WASM builds can open .lsw files.
void registerLswFormat();

} // namespace libreshockwave::moviesource
