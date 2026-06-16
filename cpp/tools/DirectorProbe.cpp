#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/format/ChunkType.hpp"
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

struct ProbeOptions {
    bool allowEmpty = false;
    bool verbose = false;
    bool showCurrent = false;
    std::size_t maxFailures = 25;
    std::size_t progressInterval = 500;
    std::vector<fs::path> roots;
};

struct ProbeSummary {
    std::size_t bytes = 0;
    std::size_t chunks = 0;
    std::size_t castMembers = 0;
    std::size_t scannedMembers = 0;
    std::size_t scripts = 0;
    std::size_t palettes = 0;
    std::size_t scoreChannels = 0;
    std::size_t scoreFrames = 0;
    std::size_t externalCasts = 0;
    int stageWidth = 0;
    int stageHeight = 0;
    int channels = 0;
    int version = 0;
    bool afterburner = false;
    std::string movieType;
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
        << " [--allow-empty] [--max-failures N] [--progress-interval N] [--show-current] [--verbose]"
        << " <file-or-directory>...\n"
        << "Scans Director .cct/.cst/.dcr/.dir/.dxr files through the native C++ runtime loader "
        << "and score metadata paths.\n"
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

ProbeOptions parseOptions(int argc, char** argv) {
    ProbeOptions options;
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

ProbeSummary probeFile(const fs::path& path) {
    const auto data = readFile(path);
    if (!hasDirectorContainerHeader(data)) {
        throw SkippedFile("not a Director container header: " + printableFourCC(data));
    }

    auto directorFile = libreshockwave::DirectorFile::load(data);
    directorFile->setBasePath(path.parent_path().string());

    const auto score = directorFile->scoreChunk();
    const auto externalCasts = directorFile->getExternalCastPaths();

    return ProbeSummary{
        data.size(),
        directorFile->chunks().size(),
        directorFile->castMembers().size(),
        directorFile->castMembers().size(),
        directorFile->scripts().size(),
        directorFile->palettes().size(),
        score == nullptr ? 0U : static_cast<std::size_t>(std::max(0, score->getChannelCount())),
        score == nullptr ? 0U : static_cast<std::size_t>(std::max(0, score->getFrameCount())),
        externalCasts.size(),
        directorFile->stageWidth(),
        directorFile->stageHeight(),
        directorFile->channelCount(),
        directorFile->version(),
        directorFile->isAfterburner(),
        libreshockwave::format::toString(directorFile->movieType()),
    };
}

void printVerboseOk(const fs::path& path, const ProbeSummary& summary) {
    std::cout << "OK " << pathString(path)
              << " version=" << summary.version
              << " movie=" << summary.movieType
              << " afterburner=" << (summary.afterburner ? "yes" : "no")
              << " stage=" << summary.stageWidth << 'x' << summary.stageHeight
              << " channels=" << summary.channels
              << " chunks=" << summary.chunks
              << " castMembers=" << summary.castMembers
              << " scannedMembers=" << summary.scannedMembers
              << " scripts=" << summary.scripts
              << " palettes=" << summary.palettes
              << " score=" << summary.scoreChannels << " channels x " << summary.scoreFrames << " frames"
              << " externalCasts=" << summary.externalCasts
              << '\n';
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

        std::cout << "Director probe: files=" << files.size() << '\n' << std::flush;

        std::size_t okCount = 0;
        std::size_t skippedCount = 0;
        std::size_t failureCount = 0;
        std::size_t totalBytes = 0;
        std::size_t totalChunks = 0;
        std::size_t totalCastMembers = 0;
        std::size_t totalScannedMembers = 0;
        std::size_t totalScripts = 0;
        std::size_t totalScoreChannels = 0;
        std::size_t totalScoreFrames = 0;

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
                std::cout << "PROBE " << (fileIndex + 1) << '/' << files.size() << ' '
                          << pathString(file) << '\n' << std::flush;
            }

            try {
                const auto summary = probeFile(file);
                ++okCount;
                totalBytes += summary.bytes;
                totalChunks += summary.chunks;
                totalCastMembers += summary.castMembers;
                totalScannedMembers += summary.scannedMembers;
                totalScripts += summary.scripts;
                totalScoreChannels += summary.scoreChannels;
                totalScoreFrames += summary.scoreFrames;

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

        std::cout << "Director probe summary: ok=" << okCount
                  << " skipped=" << skippedCount
                  << " failed=" << failureCount
                  << " bytes=" << totalBytes
                  << " chunks=" << totalChunks
                  << " castMembers=" << totalCastMembers
                  << " scannedMembers=" << totalScannedMembers
                  << " scripts=" << totalScripts
                  << " scoreChannels=" << totalScoreChannels
                  << " scoreFrames=" << totalScoreFrames
                  << '\n';

        if (okCount == 0 && !files.empty() && failureCount == 0 && !options.allowEmpty) {
            std::cerr << "No valid Director containers were loaded.\n";
            return 2;
        }
        return failureCount == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Director probe error: " << error.what() << '\n';
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }
}
