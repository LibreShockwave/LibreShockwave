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

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/PlayerState.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"
#include "libreshockwave/player/score/ScoreNavigator.hpp"
#include "libreshockwave/player/score/ScoreBehaviorRef.hpp"
#include "libreshockwave/player/behavior/BehaviorManager.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "ProbeFixtureRoots.hpp"

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 5> kDirectorExtensions{
    ".cct",
    ".cst",
    ".dcr",
    ".dir",
    ".dxr",
};

struct RenderProbeOptions {
    bool allowEmpty = false;
    bool verbose = false;
    bool showCurrent = false;
    bool play = false;
    bool preloadCasts = true;
    int ticks = 0;
    int scanFrames = 0;
    int scriptTimeoutMs = 1000;
    std::size_t maxFailures = 25;
    std::size_t progressInterval = 250;
    std::string dumpRgba;   // if set, write the post-play frame as raw RGBA to this path
    std::vector<fs::path> roots;
};

struct RenderProbeSummary {
    std::size_t bytes = 0;
    std::size_t chunks = 0;
    std::size_t externalCasts = 0;
    std::size_t loadedExternalCasts = 0;
    std::size_t fetchedExternalCasts = 0;
    std::size_t externalCastRequests = 0;
    std::size_t sprites = 0;
    std::size_t bakedSprites = 0;
    std::size_t pixels = 0;
    std::size_t nonBackgroundPixels = 0;
    std::size_t transparentPixels = 0;
    std::size_t scannedFrames = 0;
    std::size_t framesWithSprites = 0;
    std::size_t framesWithBakedSprites = 0;
    std::size_t framesWithNonBackgroundPixels = 0;
    int firstNonBackgroundFrame = 0;
    std::map<std::string, std::size_t> unbakedSpriteTypes;
    std::map<std::string, std::size_t> unbakedMemberTypes;
    std::map<std::string, std::size_t> nonRenderableSpriteTypes;
    std::map<std::string, std::size_t> nonRenderableMemberTypes;
    int stageWidth = 0;
    int stageHeight = 0;
    int frame = 0;
    int frameCount = 0;
    int version = 0;
    bool afterburner = false;
    std::string movieType;
    std::string playerState;
    std::vector<std::string> scriptErrors;
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
    return std::ranges::find(kDirectorExtensions, extension) != kDirectorExtensions.end();
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

std::size_t parseSize(std::string_view value, std::string_view optionName) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size()) {
        throw std::runtime_error("Invalid value for " + std::string(optionName) + ": " + std::string(value));
    }
    return static_cast<std::size_t>(parsed);
}

int parseNonNegativeInt(std::string_view value, std::string_view optionName) {
    const auto parsed = parseSize(value, optionName);
    if (parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Value for " + std::string(optionName) + " is too large: " + std::string(value));
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
            options.allowEmpty = true;
            continue;
        }
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
            continue;
        }
        if (arg == "--show-current") {
            options.showCurrent = true;
            continue;
        }
        if (arg == "--play") {
            options.play = true;
            continue;
        }
        if (arg == "--no-preload-casts") {
            options.preloadCasts = false;
            continue;
        }
        if (arg == "--max-failures") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-failures requires a value");
            }
            options.maxFailures = parseSize(argv[++index], "--max-failures");
            continue;
        }
        constexpr std::string_view maxFailuresPrefix = "--max-failures=";
        if (arg.starts_with(maxFailuresPrefix)) {
            options.maxFailures = parseSize(arg.substr(maxFailuresPrefix.size()), "--max-failures");
            continue;
        }
        if (arg == "--progress-interval") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--progress-interval requires a value");
            }
            options.progressInterval = parseSize(argv[++index], "--progress-interval");
            continue;
        }
        constexpr std::string_view progressIntervalPrefix = "--progress-interval=";
        if (arg.starts_with(progressIntervalPrefix)) {
            options.progressInterval = parseSize(arg.substr(progressIntervalPrefix.size()), "--progress-interval");
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
        constexpr std::string_view ticksPrefix = "--ticks=";
        if (arg.starts_with(ticksPrefix)) {
            options.ticks = parseNonNegativeInt(arg.substr(ticksPrefix.size()), "--ticks");
            options.play = true;
            continue;
        }
        if (arg == "--scan-frames") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--scan-frames requires a value");
            }
            options.scanFrames = parseNonNegativeInt(argv[++index], "--scan-frames");
            continue;
        }
        constexpr std::string_view scanFramesPrefix = "--scan-frames=";
        if (arg.starts_with(scanFramesPrefix)) {
            options.scanFrames = parseNonNegativeInt(arg.substr(scanFramesPrefix.size()), "--scan-frames");
            continue;
        }
        if (arg == "--script-timeout-ms") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--script-timeout-ms requires a value");
            }
            options.scriptTimeoutMs = parseNonNegativeInt(argv[++index], "--script-timeout-ms");
            continue;
        }
        constexpr std::string_view scriptTimeoutPrefix = "--script-timeout-ms=";
        if (arg.starts_with(scriptTimeoutPrefix)) {
            options.scriptTimeoutMs = parseNonNegativeInt(arg.substr(scriptTimeoutPrefix.size()),
                                                          "--script-timeout-ms");
            continue;
        }
        if (arg == "--dump-rgba") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--dump-rgba requires a value");
            }
            options.dumpRgba = argv[++index];
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
        std::error_code entryEc;
        const auto path = iterator->path();
        if (iterator->is_regular_file(entryEc) && !entryEc && hasDirectorExtension(path)) {
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
    std::size_t nonBackgroundPixels = 0;
    std::size_t transparentPixels = 0;
};

BitmapStats measureBitmap(const libreshockwave::player::render::pipeline::FrameSnapshot& snapshot,
                          const libreshockwave::bitmap::Bitmap& bitmap) {
    const std::uint32_t background = 0xFF000000U | static_cast<std::uint32_t>(snapshot.backgroundColor & 0xFFFFFF);
    BitmapStats stats;
    stats.pixels = bitmap.pixels().size();
    for (const auto pixel : bitmap.pixels()) {
        if ((pixel >> 24) == 0) {
            ++stats.transparentPixels;
        }
        if (pixel != background) {
            ++stats.nonBackgroundPixels;
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
                                 std::map<std::string, std::size_t>& unbakedSpriteTypes,
                                 std::map<std::string, std::size_t>& unbakedMemberTypes,
                                 std::map<std::string, std::size_t>& nonRenderableSpriteTypes,
                                 std::map<std::string, std::size_t>& nonRenderableMemberTypes) {
    for (const auto& sprite : snapshot.sprites) {
        if (sprite.bakedBitmap() != nullptr) {
            continue;
        }
        auto& spriteTypes = isExpectedNonRenderableSprite(sprite) ? nonRenderableSpriteTypes : unbakedSpriteTypes;
        auto& memberTypes = isExpectedNonRenderableSprite(sprite) ? nonRenderableMemberTypes : unbakedMemberTypes;
        ++spriteTypes[std::string(libreshockwave::player::render::pipeline::name(sprite.type()))];
        ++memberTypes[memberTypeName(sprite)];
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
    std::size_t externalCasts = 0;
    std::size_t loadedExternalCasts = 0;
    std::size_t fetchedExternalCasts = 0;
};

ExternalCastStats countExternalCastStats(libreshockwave::player::Player& player) {
    ExternalCastStats stats;
    for (const auto& [_, castLib] : player.castLibManager().castLibs()) {
        if (!castLib || !castLib->isExternal()) {
            continue;
        }
        ++stats.externalCasts;
        if (castLib->isLoaded()) {
            ++stats.loadedExternalCasts;
        }
        if (castLib->isFetched()) {
            ++stats.fetchedExternalCasts;
        }
    }
    return stats;
}

RenderProbeSummary probeFile(const fs::path& path, const RenderProbeOptions& options) {
    const auto data = readFile(path);
    if (!hasDirectorContainerHeader(data)) {
        throw SkippedFile("not a Director container header: " + printableFourCC(data));
    }

    auto directorFile = libreshockwave::DirectorFile::load(data);
    directorFile->setBasePath(path.parent_path().string());

    libreshockwave::player::Player player(directorFile);
    std::vector<std::string> scriptErrors;
    player.setErrorListener([&scriptErrors](std::string_view message, std::string_view errorDetail) {
        std::string error(message);
        if (!errorDetail.empty()) {
            error += ": ";
            error += errorDetail;
        }
        scriptErrors.push_back(std::move(error));
    });

    int externalCastRequests = 0;
    if (options.preloadCasts) {
        externalCastRequests = player.preloadAllCasts();
    }

    if (options.play) {
        player.vm().setTickDeadlineMs(options.scriptTimeoutMs);
        player.play();
        for (int tick = 0; tick < options.ticks; ++tick) {
            if (!player.tick()) {
                break;
            }
        }
    }

    const auto snapshot = player.frameSnapshot();
    std::cerr << "snapshot backgroundColor=" << (snapshot.backgroundColor & 0xFFFFFF) << "\n";
    std::cerr << "snapshot bakeTick=" << snapshot.bakeTick << "\n";
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
    if (bitmap.width() != snapshot.stageWidth || bitmap.height() != snapshot.stageHeight) {
        throw std::runtime_error("Rendered bitmap dimensions do not match frame snapshot");
    }

    if (!options.dumpRgba.empty()) {
        std::ofstream out(options.dumpRgba, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open --dump-rgba output: " + options.dumpRgba);
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

    const auto initialBitmapStats = measureBitmap(snapshot, bitmap);
    std::size_t scannedFrames = 0;
    std::size_t framesWithSprites = 0;
    std::size_t framesWithBakedSprites = 0;
    std::size_t framesWithNonBackgroundPixels = 0;
    int firstNonBackgroundFrame = 0;
    std::map<std::string, std::size_t> unbakedSpriteTypes;
    std::map<std::string, std::size_t> unbakedMemberTypes;
    std::map<std::string, std::size_t> nonRenderableSpriteTypes;
    std::map<std::string, std::size_t> nonRenderableMemberTypes;
    accumulateSpriteDiagnostics(snapshot,
                                unbakedSpriteTypes,
                                unbakedMemberTypes,
                                nonRenderableSpriteTypes,
                                nonRenderableMemberTypes);
    if (options.scanFrames > 0 && player.frameCount() > 0) {
        const int framesToScan = std::min(options.scanFrames, player.frameCount());
        for (int frame = 1; frame <= framesToScan; ++frame) {
            const auto scanSnapshot = player.frameRenderPipeline().renderFrame(frame);
            const auto scanBitmap = scanSnapshot.renderFrame();
            if (scanBitmap.width() != scanSnapshot.stageWidth || scanBitmap.height() != scanSnapshot.stageHeight) {
                throw std::runtime_error("Rendered scanned bitmap dimensions do not match frame snapshot");
            }

            ++scannedFrames;
            if (!scanSnapshot.sprites.empty()) {
                ++framesWithSprites;
            }
            if (countBakedSprites(scanSnapshot.sprites) > 0) {
                ++framesWithBakedSprites;
            }
            accumulateSpriteDiagnostics(scanSnapshot,
                                        unbakedSpriteTypes,
                                        unbakedMemberTypes,
                                        nonRenderableSpriteTypes,
                                        nonRenderableMemberTypes);
            if (measureBitmap(scanSnapshot, scanBitmap).nonBackgroundPixels > 0) {
                ++framesWithNonBackgroundPixels;
                if (firstNonBackgroundFrame == 0) {
                    firstNonBackgroundFrame = frame;
                }
            }
        }
    }

    const auto externalCastStats = countExternalCastStats(player);
    return RenderProbeSummary{
        data.size(),
        directorFile->chunks().size(),
        externalCastStats.externalCasts,
        externalCastStats.loadedExternalCasts,
        externalCastStats.fetchedExternalCasts,
        static_cast<std::size_t>(externalCastRequests),
        snapshot.sprites.size(),
        countBakedSprites(snapshot.sprites),
        initialBitmapStats.pixels,
        initialBitmapStats.nonBackgroundPixels,
        initialBitmapStats.transparentPixels,
        scannedFrames,
        framesWithSprites,
        framesWithBakedSprites,
        framesWithNonBackgroundPixels,
        firstNonBackgroundFrame,
        std::move(unbakedSpriteTypes),
        std::move(unbakedMemberTypes),
        std::move(nonRenderableSpriteTypes),
        std::move(nonRenderableMemberTypes),
        snapshot.stageWidth,
        snapshot.stageHeight,
        player.currentFrame(),
        player.frameCount(),
        directorFile->version(),
        directorFile->isAfterburner(),
        libreshockwave::format::toString(directorFile->movieType()),
        std::string(libreshockwave::player::name(player.state())),
        std::move(scriptErrors),
    };
}

void printVerboseOk(const fs::path& path, const RenderProbeSummary& summary) {
    std::cout << "OK " << pathString(path)
              << " version=" << summary.version
              << " movie=" << summary.movieType
              << " afterburner=" << (summary.afterburner ? "yes" : "no")
              << " stage=" << summary.stageWidth << 'x' << summary.stageHeight
              << " frame=" << summary.frame << '/' << summary.frameCount
              << " state=" << summary.playerState
              << " chunks=" << summary.chunks
              << " externalCasts=" << summary.externalCasts
              << " loadedExternalCasts=" << summary.loadedExternalCasts
              << " fetchedExternalCasts=" << summary.fetchedExternalCasts
              << " externalCastRequests=" << summary.externalCastRequests
              << " sprites=" << summary.sprites
              << " bakedSprites=" << summary.bakedSprites
              << " pixels=" << summary.pixels
              << " nonBackgroundPixels=" << summary.nonBackgroundPixels
              << " transparentPixels=" << summary.transparentPixels
              << " scannedFrames=" << summary.scannedFrames
              << " framesWithSprites=" << summary.framesWithSprites
              << " framesWithBakedSprites=" << summary.framesWithBakedSprites
              << " framesWithNonBackgroundPixels=" << summary.framesWithNonBackgroundPixels
              << " firstNonBackgroundFrame=" << summary.firstNonBackgroundFrame
              << " unbakedSpriteTypes=" << formatCounts(summary.unbakedSpriteTypes)
              << " unbakedMemberTypes=" << formatCounts(summary.unbakedMemberTypes)
              << " nonRenderableSpriteTypes=" << formatCounts(summary.nonRenderableSpriteTypes)
              << " nonRenderableMemberTypes=" << formatCounts(summary.nonRenderableMemberTypes)
              << " scriptErrors=" << summary.scriptErrors.size()
              << '\n';

    for (const auto& error : summary.scriptErrors) {
        std::cout << "SCRIPT_ERROR " << pathString(path) << ": " << error << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parseOptions(argc, argv);
        const auto files = collectFiles(options.roots);
        if (files.empty() && !options.allowEmpty) {
            std::cerr << "No Director files found. Use --allow-empty to treat this as success.\n";
            return 2;
        }

        std::cout << "Director render probe: files=" << files.size()
                  << " preloadCasts=" << (options.preloadCasts ? "yes" : "no")
                  << " play=" << (options.play ? "yes" : "no")
                  << " ticks=" << options.ticks
                  << " scanFrames=" << options.scanFrames
                  << " scriptTimeoutMs=" << (options.play ? options.scriptTimeoutMs : 0)
                  << '\n' << std::flush;

        std::size_t okCount = 0;
        std::size_t skippedCount = 0;
        std::size_t failureCount = 0;
        std::size_t totalBytes = 0;
        std::size_t totalChunks = 0;
        std::size_t totalExternalCasts = 0;
        std::size_t totalLoadedExternalCasts = 0;
        std::size_t totalFetchedExternalCasts = 0;
        std::size_t totalExternalCastRequests = 0;
        std::size_t totalSprites = 0;
        std::size_t totalBakedSprites = 0;
        std::size_t totalPixels = 0;
        std::size_t totalNonBackgroundPixels = 0;
        std::size_t totalTransparentPixels = 0;
        std::size_t totalScannedFrames = 0;
        std::size_t totalFramesWithSprites = 0;
        std::size_t totalFramesWithBakedSprites = 0;
        std::size_t totalFramesWithNonBackgroundPixels = 0;
        std::size_t filesWithNonBackgroundScannedFrames = 0;
        std::size_t totalScriptErrors = 0;

        const auto printProgress = [&](std::size_t processedCount) {
            if (options.verbose || options.progressInterval == 0) {
                return;
            }
            if (processedCount % options.progressInterval != 0 && processedCount != files.size()) {
                return;
            }
            std::cout << "PROGRESS " << processedCount << '/' << files.size()
                      << " ok=" << okCount
                      << " skipped=" << skippedCount
                      << " failed=" << failureCount
                      << '\n' << std::flush;
        };

        for (std::size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
            const auto& file = files[fileIndex];
            bool stopAfterFile = false;
            if (options.showCurrent || options.verbose) {
                std::cout << "RENDER " << (fileIndex + 1) << '/' << files.size() << ' '
                          << pathString(file) << '\n' << std::flush;
            }

            try {
                const auto summary = probeFile(file, options);
                ++okCount;
                totalBytes += summary.bytes;
                totalChunks += summary.chunks;
                totalExternalCasts += summary.externalCasts;
                totalLoadedExternalCasts += summary.loadedExternalCasts;
                totalFetchedExternalCasts += summary.fetchedExternalCasts;
                totalExternalCastRequests += summary.externalCastRequests;
                totalSprites += summary.sprites;
                totalBakedSprites += summary.bakedSprites;
                totalPixels += summary.pixels;
                totalNonBackgroundPixels += summary.nonBackgroundPixels;
                totalTransparentPixels += summary.transparentPixels;
                totalScannedFrames += summary.scannedFrames;
                totalFramesWithSprites += summary.framesWithSprites;
                totalFramesWithBakedSprites += summary.framesWithBakedSprites;
                totalFramesWithNonBackgroundPixels += summary.framesWithNonBackgroundPixels;
                if (summary.firstNonBackgroundFrame > 0) {
                    ++filesWithNonBackgroundScannedFrames;
                }
                totalScriptErrors += summary.scriptErrors.size();

                if (options.verbose) {
                    printVerboseOk(file, summary);
                }
            } catch (const SkippedFile& skipped) {
                ++skippedCount;
                if (options.verbose) {
                    std::cout << "SKIP " << pathString(file) << ": " << skipped.what() << '\n' << std::flush;
                }
            } catch (const std::exception& error) {
                ++failureCount;
                std::cerr << "FAIL " << pathString(file) << ": " << error.what() << '\n';
                if (options.maxFailures != 0 && failureCount >= options.maxFailures) {
                    std::cerr << "Stopping after " << failureCount << " failures.\n";
                    stopAfterFile = true;
                }
            }

            printProgress(fileIndex + 1);
            if (stopAfterFile) {
                break;
            }
        }

        std::cout << "Director render probe summary: ok=" << okCount
                  << " skipped=" << skippedCount
                  << " failed=" << failureCount
                  << " bytes=" << totalBytes
                  << " chunks=" << totalChunks
                  << " externalCasts=" << totalExternalCasts
                  << " loadedExternalCasts=" << totalLoadedExternalCasts
                  << " fetchedExternalCasts=" << totalFetchedExternalCasts
                  << " externalCastRequests=" << totalExternalCastRequests
                  << " sprites=" << totalSprites
                  << " bakedSprites=" << totalBakedSprites
                  << " pixels=" << totalPixels
                  << " nonBackgroundPixels=" << totalNonBackgroundPixels
                  << " transparentPixels=" << totalTransparentPixels
                  << " scannedFrames=" << totalScannedFrames
                  << " framesWithSprites=" << totalFramesWithSprites
                  << " framesWithBakedSprites=" << totalFramesWithBakedSprites
                  << " framesWithNonBackgroundPixels=" << totalFramesWithNonBackgroundPixels
                  << " filesWithNonBackgroundScannedFrames=" << filesWithNonBackgroundScannedFrames
                  << " scriptErrors=" << totalScriptErrors
                  << '\n';

        if (okCount == 0 && !files.empty() && failureCount == 0 && !options.allowEmpty) {
            std::cerr << "No valid Director containers were rendered.\n";
            return 2;
        }
        return failureCount == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Director render probe error: " << error.what() << '\n';
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }
}
