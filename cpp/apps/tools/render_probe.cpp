#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/player_state.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"
#include "libreshockwave/player/score/score_navigator.hpp"
#include "libreshockwave/player/score/score_behavior_ref.hpp"
#include "libreshockwave/player/behavior/behavior_manager.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "probe_fixture_roots.hpp"

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 5> k_director_extensions{
    ".cct",
    ".cst",
    ".dcr",
    ".dir",
    ".dxr",
};

struct RenderProbeOptions {
    bool allow_empty = false;
    bool verbose = false;
    bool show_current = false;
    bool play = false;
    bool preload_casts = true;
    int ticks = 0;
    int scan_frames = 0;
    int script_timeout_ms = 1000;
    std::size_t max_failures = 25;
    std::size_t progress_interval = 250;
    std::string dump_rgba;   // if set, write the post-play frame as raw RGBA to this path
    std::vector<fs::path> roots;
};

struct RenderProbeSummary {
    std::size_t bytes = 0;
    std::size_t chunks = 0;
    std::size_t external_casts = 0;
    std::size_t loaded_external_casts = 0;
    std::size_t fetched_external_casts = 0;
    std::size_t external_cast_requests = 0;
    std::size_t sprites = 0;
    std::size_t baked_sprites = 0;
    std::size_t pixels = 0;
    std::size_t non_background_pixels = 0;
    std::size_t transparent_pixels = 0;
    std::size_t scanned_frames = 0;
    std::size_t frames_with_sprites = 0;
    std::size_t frames_with_baked_sprites = 0;
    std::size_t frames_with_non_background_pixels = 0;
    int first_non_background_frame = 0;
    std::map<std::string, std::size_t> unbaked_sprite_types;
    std::map<std::string, std::size_t> unbaked_member_types;
    std::map<std::string, std::size_t> non_renderable_sprite_types;
    std::map<std::string, std::size_t> non_renderable_member_types;
    int stage_width = 0;
    int stage_height = 0;
    int frame = 0;
    int frame_count = 0;
    int version = 0;
    bool afterburner = false;
    std::string movie_type;
    std::string player_state;
    std::vector<std::string> script_errors;
};

class SkippedFile : public std::runtime_error {
public:
    explicit SkippedFile(const std::string& message) : std::runtime_error(message) {}
};

std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool hasDirectorExtension(const fs::path& path) {
    const auto extension = toLower(path.extension().string());
    return std::ranges::find(k_director_extensions, extension) != k_director_extensions.end();
}

std::string pathString(const fs::path& path) {
    return path.lexically_normal().string();
}

std::string printableFourCC(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return "<short>";
    }

    std::string result;
    result.reserve(4);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto ch = data[index];
        result.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.');
    }
    return result;
}

bool hasDirectorContainerHeader(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }

    const std::string_view header(reinterpret_cast<const char*>(data.data()), 4);
    return header == "RIFX" || header == "XFIR" || header == "RIFF" || header == "FFIR";
}

std::string usage(const char* argv0) {
    std::ostringstream out;
    out << "Usage: " << argv0
        << " [--allow-empty] [--max-failures N] [--progress-interval N] [--show-current]"
        << " [--verbose] [--no-preload-casts] [--play] [--ticks N] [--script-timeout-ms N]"
        << " [--scan-frames N] <file-or-directory>...\n"
        << "Loads Director .cct/.cst/.dcr/.dir/.dxr files through the native C++ player renderer "
        << "and reports frame-buffer statistics. By default local external casts are preloaded from "
        << "the movie directory but scripts are not run; --play prepares the movie foundation, "
        << "and --ticks N advances N playback ticks before rendering. "
        << "--scan-frames N also renders up to N score frames and reports nonblank frame coverage. "
        << "Lifecycle script dispatch is capped by --script-timeout-ms when --play is active; use 0 to disable it.\n"
        << "If no paths are supplied, /var/html is used when present; otherwise /var/www/html is used "
        << "as the local fixture mapping.";
    return out.str();
}

std::size_t parseSize(std::string_view value, std::string_view option_name) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size()) {
        throw std::runtime_error("Invalid value for " + std::string(option_name) + ": " + std::string(value));
    }
    return static_cast<std::size_t>(parsed);
}

int parseNonNegativeInt(std::string_view value, std::string_view option_name) {
    const auto parsed = parseSize(value, option_name);
    if (parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Value for " + std::string(option_name) + " is too large: " + std::string(value));
    }
    return static_cast<int>(parsed);
}

RenderProbeOptions parseOptions(int argc, char** argv) {
    RenderProbeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--help" || arg == "-h") {
            std::cout << usage(argv[0]) << '\n';
            std::exit(0);
        }
        if (arg == "--allow-empty") {
            options.allow_empty = true;
            continue;
        }
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
            continue;
        }
        if (arg == "--show-current") {
            options.show_current = true;
            continue;
        }
        if (arg == "--play") {
            options.play = true;
            continue;
        }
        if (arg == "--no-preload-casts") {
            options.preload_casts = false;
            continue;
        }
        if (arg == "--max-failures") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-failures requires a value");
            }
            options.max_failures = parseSize(argv[++index], "--max-failures");
            continue;
        }
        constexpr std::string_view max_failures_prefix = "--max-failures=";
        if (arg.starts_with(max_failures_prefix)) {
            options.max_failures = parseSize(arg.substr(max_failures_prefix.size()), "--max-failures");
            continue;
        }
        if (arg == "--progress-interval") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--progress-interval requires a value");
            }
            options.progress_interval = parseSize(argv[++index], "--progress-interval");
            continue;
        }
        constexpr std::string_view progress_interval_prefix = "--progress-interval=";
        if (arg.starts_with(progress_interval_prefix)) {
            options.progress_interval = parseSize(arg.substr(progress_interval_prefix.size()), "--progress-interval");
            continue;
        }
        if (arg == "--ticks") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--ticks requires a value");
            }
            options.ticks = parseNonNegativeInt(argv[++index], "--ticks");
            options.play = true;
            continue;
        }
        constexpr std::string_view ticks_prefix = "--ticks=";
        if (arg.starts_with(ticks_prefix)) {
            options.ticks = parseNonNegativeInt(arg.substr(ticks_prefix.size()), "--ticks");
            options.play = true;
            continue;
        }
        if (arg == "--scan-frames") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--scan-frames requires a value");
            }
            options.scan_frames = parseNonNegativeInt(argv[++index], "--scan-frames");
            continue;
        }
        constexpr std::string_view scan_frames_prefix = "--scan-frames=";
        if (arg.starts_with(scan_frames_prefix)) {
            options.scan_frames = parseNonNegativeInt(arg.substr(scan_frames_prefix.size()), "--scan-frames");
            continue;
        }
        if (arg == "--script-timeout-ms") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--script-timeout-ms requires a value");
            }
            options.script_timeout_ms = parseNonNegativeInt(argv[++index], "--script-timeout-ms");
            continue;
        }
        constexpr std::string_view script_timeout_prefix = "--script-timeout-ms=";
        if (arg.starts_with(script_timeout_prefix)) {
            options.script_timeout_ms = parseNonNegativeInt(arg.substr(script_timeout_prefix.size()),
                                                          "--script-timeout-ms");
            continue;
        }
        if (arg == "--dump-rgba") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--dump-rgba requires a value");
            }
            options.dump_rgba = argv[++index];
            continue;
        }
        if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + std::string(arg));
        }
        options.roots.emplace_back(arg);
    }

    if (options.roots.empty()) {
        options.roots = libreshockwave::tools::defaultFixtureRoots();
    }
    return options;
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file");
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end == std::ifstream::pos_type(-1)) {
        throw std::runtime_error("Unable to determine file size");
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) {
            throw std::runtime_error("Unable to read complete file");
        }
    }
    return data;
}

void appendRootFiles(const fs::path& root, std::vector<fs::path>& files) {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        throw std::runtime_error("Path does not exist: " + pathString(root));
    }

    if (fs::is_regular_file(root, ec)) {
        if (hasDirectorExtension(root)) {
            files.push_back(root);
        }
        return;
    }

    if (!fs::is_directory(root, ec)) {
        return;
    }

    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && iterator != end) {
        std::error_code entry_ec;
        const auto path = iterator->path();
        if (iterator->is_regular_file(entry_ec) && !entry_ec && hasDirectorExtension(path)) {
            files.push_back(path);
        }
        iterator.increment(ec);
    }
    if (ec) {
        throw std::runtime_error("Unable to scan directory " + pathString(root) + ": " + ec.message());
    }
}

std::vector<fs::path> collectFiles(const std::vector<fs::path>& roots) {
    std::vector<fs::path> files;
    for (const auto& root : roots) {
        appendRootFiles(root, files);
    }
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

std::size_t countBakedSprites(const std::vector<libreshockwave::player::render::pipeline::RenderSprite>& sprites) {
    return static_cast<std::size_t>(std::ranges::count_if(sprites, [](const auto& sprite) {
        return sprite.bakedBitmap() != nullptr;
    }));
}

struct BitmapStats {
    std::size_t pixels = 0;
    std::size_t non_background_pixels = 0;
    std::size_t transparent_pixels = 0;
};

BitmapStats measureBitmap(const libreshockwave::player::render::pipeline::FrameSnapshot& snapshot,
                          const libreshockwave::bitmap::Bitmap& bitmap) {
    const std::uint32_t background = 0xFF000000U | static_cast<std::uint32_t>(snapshot.background_color & 0xFFFFFF);
    BitmapStats stats;
    stats.pixels = bitmap.pixels().size();
    for (const auto pixel : bitmap.pixels()) {
        if ((pixel >> 24) == 0) {
            ++stats.transparent_pixels;
        }
        if (pixel != background) {
            ++stats.non_background_pixels;
        }
    }
    return stats;
}

std::string memberTypeName(const libreshockwave::player::render::pipeline::RenderSprite& sprite) {
    if (const auto member = sprite.castMember()) {
        return std::string(libreshockwave::cast::name(member->memberType()));
    }
    if (sprite.dynamicMember() != nullptr) {
        return std::string(libreshockwave::cast::name(sprite.dynamicMember()->memberType()));
    }
    return "missing";
}

bool isExpectedNonRenderableSprite(const libreshockwave::player::render::pipeline::RenderSprite& sprite) {
    if (sprite.castMember() == nullptr && sprite.dynamicMember() == nullptr) {
        return true;
    }
    const auto member = sprite.castMember();
    if (member != nullptr) {
        return member->memberType() == libreshockwave::cast::MemberType::Script ||
               member->memberType() == libreshockwave::cast::MemberType::Shockwave3D;
    }
    return sprite.dynamicMember() != nullptr &&
           sprite.dynamicMember()->memberType() == libreshockwave::cast::MemberType::Shockwave3D;
}

void accumulateSpriteDiagnostics(const libreshockwave::player::render::pipeline::FrameSnapshot& snapshot,
                                 std::map<std::string, std::size_t>& unbaked_sprite_types,
                                 std::map<std::string, std::size_t>& unbaked_member_types,
                                 std::map<std::string, std::size_t>& non_renderable_sprite_types,
                                 std::map<std::string, std::size_t>& non_renderable_member_types) {
    for (const auto& sprite : snapshot.sprites) {
        if (sprite.bakedBitmap() != nullptr) {
            continue;
        }
        auto& sprite_types = isExpectedNonRenderableSprite(sprite) ? non_renderable_sprite_types : unbaked_sprite_types;
        auto& member_types = isExpectedNonRenderableSprite(sprite) ? non_renderable_member_types : unbaked_member_types;
        ++sprite_types[std::string(libreshockwave::player::render::pipeline::name(sprite.type()))];
        ++member_types[memberTypeName(sprite)];
    }
}

std::string formatCounts(const std::map<std::string, std::size_t>& counts) {
    if (counts.empty()) {
        return "{}";
    }
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto& [name, count] : counts) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << name << ':' << count;
    }
    out << '}';
    return out.str();
}

struct ExternalCastStats {
    std::size_t external_casts = 0;
    std::size_t loaded_external_casts = 0;
    std::size_t fetched_external_casts = 0;
};

ExternalCastStats countExternalCastStats(libreshockwave::player::Player& player) {
    ExternalCastStats stats;
    for (const auto& [_, cast_lib] : player.castLibManager().castLibs()) {
        if (!cast_lib || !cast_lib->isExternal()) {
            continue;
        }
        ++stats.external_casts;
        if (cast_lib->isLoaded()) {
            ++stats.loaded_external_casts;
        }
        if (cast_lib->isFetched()) {
            ++stats.fetched_external_casts;
        }
    }
    return stats;
}

RenderProbeSummary probeFile(const fs::path& path, const RenderProbeOptions& options) {
    const auto data = readFile(path);
    if (!hasDirectorContainerHeader(data)) {
        throw SkippedFile("not a Director container header: " + printableFourCC(data));
    }

    auto director_file = libreshockwave::DirectorFile::load(data);
    director_file->setBasePath(path.parent_path().string());

    libreshockwave::player::Player player(director_file);
    std::vector<std::string> script_errors;
    player.setErrorListener([&script_errors](std::string_view message, std::string_view error_detail) {
        std::string error(message);
        if (!error_detail.empty()) {
            error += ": ";
            error += error_detail;
        }
        script_errors.push_back(std::move(error));
    });

    int external_cast_requests = 0;
    if (options.preload_casts) {
        external_cast_requests = player.preloadAllCasts();
    }

    if (options.play) {
        player.vm().setTickDeadlineMs(options.script_timeout_ms);
        player.play();
        for (int tick = 0; tick < options.ticks; ++tick) {
            if (!player.tick()) {
                break;
            }
        }
    }

    const auto snapshot = player.frameSnapshot();
    std::cerr << "snapshot backgroundColor=" << (snapshot.background_color & 0xFFFFFF) << "\n";
    std::cerr << "snapshot bakeTick=" << snapshot.bake_tick << "\n";
    std::cerr << "sprite count=" << snapshot.sprites.size() << "\n";
    std::cerr << "currentFrame=" << player.currentFrame() << "\n";
    {
        const auto& nav = player.navigator();
        const int cf = player.currentFrame();
        const auto active = nav.getActiveChannels(cf);
        std::cerr << "active channels on frame " << cf << ":";
        for (int ch : active) std::cerr << " " << ch;
        std::cerr << "\n";
        for (int ch : active) {
            auto bh = nav.getSpriteBehaviors(cf, ch);
            if (!bh.empty()) {
                std::cerr << "behaviors for ch" << ch << ":";
                for (const auto& b : bh) {
                    std::cerr << " castLib=" << b.castLib() << " member=" << b.castMember();
                }
                std::cerr << "\n";
            }
        }
        if (const auto* fs = nav.getFrameScript(cf); fs) {
            std::cerr << "frame script on frame " << cf << ": castLib=" << fs->castLib() << " member=" << fs->castMember() << "\n";
        }
        std::cerr << "behavior instances for ch10:";
        for (const auto& inst : player.behaviorManager().getInstancesForChannel(10)) {
            std::cerr << " id=" << inst->id();
            if (inst->script()) std::cerr << " script=" << inst->script()->scriptName();
        }
        std::cerr << "\n";
    }
    for (const auto& s : snapshot.sprites) {
        const int ch = s.channel();
        if (ch >= 1 && ch <= 12) {
            std::cerr << "sprite ch=" << ch << " x=" << s.x() << " y=" << s.y()
                      << " w=" << s.width() << " h=" << s.height()
                      << " ink=" << static_cast<int>(libreshockwave::id::code(s.inkMode()))
                      << " blend=" << s.blend() << " visible=" << s.isVisible()
                      << " type=" << libreshockwave::player::render::pipeline::name(s.type())
                      << " baked=" << (s.bakedBitmap() != nullptr) << "\n";
        }
        if (ch == 81 || ch == 33 || ch == 34 || ch == 35) {
            std::cerr << "sprite ch=" << ch << " x=" << s.x() << " y=" << s.y()
                      << " w=" << s.width() << " h=" << s.height()
                      << " ink=" << static_cast<int>(libreshockwave::id::code(s.inkMode()))
                      << " blend=" << s.blend() << " visible=" << s.isVisible()
                      << " type=" << libreshockwave::player::render::pipeline::name(s.type())
                      << " baked=" << (s.bakedBitmap() != nullptr) << "\n";
        }
        if (ch == 10) {
            std::cerr << "sprite ch=10 memberId=" << s.castMemberId();
            if (auto mn = s.memberName(); mn && !mn->empty()) std::cerr << " name=" << *mn;
            if (s.castMember()) {
                std::cerr << " memberType=" << static_cast<int>(s.castMember()->type());
            }
            std::cerr << "\n";
        }
    }
    const auto bitmap = snapshot.renderFrame();
    if (bitmap.width() != snapshot.stage_width || bitmap.height() != snapshot.stage_height) {
        throw std::runtime_error("Rendered bitmap dimensions do not match frame snapshot");
    }

    if (!options.dump_rgba.empty()) {
        std::ofstream out(options.dump_rgba, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open --dump-rgba output: " + options.dump_rgba);
        }
        const auto& px = bitmap.pixels();
        std::string rgba;
        rgba.reserve(px.size() * 4);
        for (const auto argb : px) {
            rgba.push_back(static_cast<char>((argb >> 16) & 0xFF));  // R
            rgba.push_back(static_cast<char>((argb >> 8) & 0xFF));   // G
            rgba.push_back(static_cast<char>(argb & 0xFF));          // B
            rgba.push_back(static_cast<char>((argb >> 24) & 0xFF));  // A
        }
        out.write(rgba.data(), static_cast<std::streamsize>(rgba.size()));
    }

    const auto initial_bitmap_stats = measureBitmap(snapshot, bitmap);
    std::size_t scanned_frames = 0;
    std::size_t frames_with_sprites = 0;
    std::size_t frames_with_baked_sprites = 0;
    std::size_t frames_with_non_background_pixels = 0;
    int first_non_background_frame = 0;
    std::map<std::string, std::size_t> unbaked_sprite_types;
    std::map<std::string, std::size_t> unbaked_member_types;
    std::map<std::string, std::size_t> non_renderable_sprite_types;
    std::map<std::string, std::size_t> non_renderable_member_types;
    accumulateSpriteDiagnostics(snapshot,
                                unbaked_sprite_types,
                                unbaked_member_types,
                                non_renderable_sprite_types,
                                non_renderable_member_types);
    if (options.scan_frames > 0 && player.frameCount() > 0) {
        const int frames_to_scan = std::min(options.scan_frames, player.frameCount());
        for (int frame = 1; frame <= frames_to_scan; ++frame) {
            const auto scan_snapshot = player.frameRenderPipeline().renderFrame(frame);
            const auto scan_bitmap = scan_snapshot.renderFrame();
            if (scan_bitmap.width() != scan_snapshot.stage_width || scan_bitmap.height() != scan_snapshot.stage_height) {
                throw std::runtime_error("Rendered scanned bitmap dimensions do not match frame snapshot");
            }

            ++scanned_frames;
            if (!scan_snapshot.sprites.empty()) {
                ++frames_with_sprites;
            }
            if (countBakedSprites(scan_snapshot.sprites) > 0) {
                ++frames_with_baked_sprites;
            }
            accumulateSpriteDiagnostics(scan_snapshot,
                                        unbaked_sprite_types,
                                        unbaked_member_types,
                                        non_renderable_sprite_types,
                                        non_renderable_member_types);
            if (measureBitmap(scan_snapshot, scan_bitmap).non_background_pixels > 0) {
                ++frames_with_non_background_pixels;
                if (first_non_background_frame == 0) {
                    first_non_background_frame = frame;
                }
            }
        }
    }

    const auto external_cast_stats = countExternalCastStats(player);
    return RenderProbeSummary{
        data.size(),
        director_file->chunks().size(),
        external_cast_stats.external_casts,
        external_cast_stats.loaded_external_casts,
        external_cast_stats.fetched_external_casts,
        static_cast<std::size_t>(external_cast_requests),
        snapshot.sprites.size(),
        countBakedSprites(snapshot.sprites),
        initial_bitmap_stats.pixels,
        initial_bitmap_stats.non_background_pixels,
        initial_bitmap_stats.transparent_pixels,
        scanned_frames,
        frames_with_sprites,
        frames_with_baked_sprites,
        frames_with_non_background_pixels,
        first_non_background_frame,
        std::move(unbaked_sprite_types),
        std::move(unbaked_member_types),
        std::move(non_renderable_sprite_types),
        std::move(non_renderable_member_types),
        snapshot.stage_width,
        snapshot.stage_height,
        player.currentFrame(),
        player.frameCount(),
        director_file->version(),
        director_file->isAfterburner(),
        libreshockwave::format::toString(director_file->movieType()),
        std::string(libreshockwave::player::name(player.state())),
        std::move(script_errors),
    };
}

void printVerboseOk(const fs::path& path, const RenderProbeSummary& summary) {
    std::cout << "OK " << pathString(path)
              << " version=" << summary.version
              << " movie=" << summary.movie_type
              << " afterburner=" << (summary.afterburner ? "yes" : "no")
              << " stage=" << summary.stage_width << 'x' << summary.stage_height
              << " frame=" << summary.frame << '/' << summary.frame_count
              << " state=" << summary.player_state
              << " chunks=" << summary.chunks
              << " externalCasts=" << summary.external_casts
              << " loadedExternalCasts=" << summary.loaded_external_casts
              << " fetchedExternalCasts=" << summary.fetched_external_casts
              << " externalCastRequests=" << summary.external_cast_requests
              << " sprites=" << summary.sprites
              << " bakedSprites=" << summary.baked_sprites
              << " pixels=" << summary.pixels
              << " nonBackgroundPixels=" << summary.non_background_pixels
              << " transparentPixels=" << summary.transparent_pixels
              << " scannedFrames=" << summary.scanned_frames
              << " framesWithSprites=" << summary.frames_with_sprites
              << " framesWithBakedSprites=" << summary.frames_with_baked_sprites
              << " framesWithNonBackgroundPixels=" << summary.frames_with_non_background_pixels
              << " firstNonBackgroundFrame=" << summary.first_non_background_frame
              << " unbakedSpriteTypes=" << formatCounts(summary.unbaked_sprite_types)
              << " unbakedMemberTypes=" << formatCounts(summary.unbaked_member_types)
              << " nonRenderableSpriteTypes=" << formatCounts(summary.non_renderable_sprite_types)
              << " nonRenderableMemberTypes=" << formatCounts(summary.non_renderable_member_types)
              << " scriptErrors=" << summary.script_errors.size()
              << '\n';

    for (const auto& error : summary.script_errors) {
        std::cout << "SCRIPT_ERROR " << pathString(path) << ": " << error << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parseOptions(argc, argv);
        const auto files = collectFiles(options.roots);
        if (files.empty() && !options.allow_empty) {
            std::cerr << "No Director files found. Use --allow-empty to treat this as success.\n";
            return 2;
        }

        std::cout << "Director render probe: files=" << files.size()
                  << " preloadCasts=" << (options.preload_casts ? "yes" : "no")
                  << " play=" << (options.play ? "yes" : "no")
                  << " ticks=" << options.ticks
                  << " scanFrames=" << options.scan_frames
                  << " scriptTimeoutMs=" << (options.play ? options.script_timeout_ms : 0)
                  << '\n' << std::flush;

        std::size_t ok_count = 0;
        std::size_t skipped_count = 0;
        std::size_t failure_count = 0;
        std::size_t total_bytes = 0;
        std::size_t total_chunks = 0;
        std::size_t total_external_casts = 0;
        std::size_t total_loaded_external_casts = 0;
        std::size_t total_fetched_external_casts = 0;
        std::size_t total_external_cast_requests = 0;
        std::size_t total_sprites = 0;
        std::size_t total_baked_sprites = 0;
        std::size_t total_pixels = 0;
        std::size_t total_non_background_pixels = 0;
        std::size_t total_transparent_pixels = 0;
        std::size_t total_scanned_frames = 0;
        std::size_t total_frames_with_sprites = 0;
        std::size_t total_frames_with_baked_sprites = 0;
        std::size_t total_frames_with_non_background_pixels = 0;
        std::size_t files_with_non_background_scanned_frames = 0;
        std::size_t total_script_errors = 0;

        const auto print_progress = [&](std::size_t processed_count) {
            if (options.verbose || options.progress_interval == 0) {
                return;
            }
            if (processed_count % options.progress_interval != 0 && processed_count != files.size()) {
                return;
            }
            std::cout << "PROGRESS " << processed_count << '/' << files.size()
                      << " ok=" << ok_count
                      << " skipped=" << skipped_count
                      << " failed=" << failure_count
                      << '\n' << std::flush;
        };

        for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
            const auto& file = files[file_index];
            bool stop_after_file = false;
            if (options.show_current || options.verbose) {
                std::cout << "RENDER " << (file_index + 1) << '/' << files.size() << ' '
                          << pathString(file) << '\n' << std::flush;
            }

            try {
                const auto summary = probeFile(file, options);
                ++ok_count;
                total_bytes += summary.bytes;
                total_chunks += summary.chunks;
                total_external_casts += summary.external_casts;
                total_loaded_external_casts += summary.loaded_external_casts;
                total_fetched_external_casts += summary.fetched_external_casts;
                total_external_cast_requests += summary.external_cast_requests;
                total_sprites += summary.sprites;
                total_baked_sprites += summary.baked_sprites;
                total_pixels += summary.pixels;
                total_non_background_pixels += summary.non_background_pixels;
                total_transparent_pixels += summary.transparent_pixels;
                total_scanned_frames += summary.scanned_frames;
                total_frames_with_sprites += summary.frames_with_sprites;
                total_frames_with_baked_sprites += summary.frames_with_baked_sprites;
                total_frames_with_non_background_pixels += summary.frames_with_non_background_pixels;
                if (summary.first_non_background_frame > 0) {
                    ++files_with_non_background_scanned_frames;
                }
                total_script_errors += summary.script_errors.size();

                if (options.verbose) {
                    printVerboseOk(file, summary);
                }
            } catch (const SkippedFile& skipped) {
                ++skipped_count;
                if (options.verbose) {
                    std::cout << "SKIP " << pathString(file) << ": " << skipped.what() << '\n' << std::flush;
                }
            } catch (const std::exception& error) {
                ++failure_count;
                std::cerr << "FAIL " << pathString(file) << ": " << error.what() << '\n';
                if (options.max_failures != 0 && failure_count >= options.max_failures) {
                    std::cerr << "Stopping after " << failure_count << " failures.\n";
                    stop_after_file = true;
                }
            }

            print_progress(file_index + 1);
            if (stop_after_file) {
                break;
            }
        }

        std::cout << "Director render probe summary: ok=" << ok_count
                  << " skipped=" << skipped_count
                  << " failed=" << failure_count
                  << " bytes=" << total_bytes
                  << " chunks=" << total_chunks
                  << " externalCasts=" << total_external_casts
                  << " loadedExternalCasts=" << total_loaded_external_casts
                  << " fetchedExternalCasts=" << total_fetched_external_casts
                  << " externalCastRequests=" << total_external_cast_requests
                  << " sprites=" << total_sprites
                  << " bakedSprites=" << total_baked_sprites
                  << " pixels=" << total_pixels
                  << " nonBackgroundPixels=" << total_non_background_pixels
                  << " transparentPixels=" << total_transparent_pixels
                  << " scannedFrames=" << total_scanned_frames
                  << " framesWithSprites=" << total_frames_with_sprites
                  << " framesWithBakedSprites=" << total_frames_with_baked_sprites
                  << " framesWithNonBackgroundPixels=" << total_frames_with_non_background_pixels
                  << " filesWithNonBackgroundScannedFrames=" << files_with_non_background_scanned_frames
                  << " scriptErrors=" << total_script_errors
                  << '\n';

        if (ok_count == 0 && !files.empty() && failure_count == 0 && !options.allow_empty) {
            std::cerr << "No valid Director containers were rendered.\n";
            return 2;
        }
        return failure_count == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Director render probe error: " << error.what() << '\n';
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }
}
