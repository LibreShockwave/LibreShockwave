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

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/format/chunk_type.hpp"
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

struct ProbeOptions {
    bool allow_empty = false;
    bool verbose = false;
    bool show_current = false;
    std::size_t max_failures = 25;
    std::size_t progress_interval = 500;
    std::vector<fs::path> roots;
};

struct ProbeSummary {
    std::size_t bytes = 0;
    std::size_t chunks = 0;
    std::size_t cast_members = 0;
    std::size_t scanned_members = 0;
    std::size_t scripts = 0;
    std::size_t palettes = 0;
    std::size_t score_channels = 0;
    std::size_t score_frames = 0;
    std::size_t external_casts = 0;
    int stage_width = 0;
    int stage_height = 0;
    int channels = 0;
    int version = 0;
    bool afterburner = false;
    std::string movie_type;
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
        << " [--allow-empty] [--max-failures N] [--progress-interval N] [--show-current] [--verbose]"
        << " <file-or-directory>...\n"
        << "Scans Director .cct/.cst/.dcr/.dir/.dxr files through the native C++ runtime loader "
        << "and score metadata paths.\n"
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

ProbeOptions parseOptions(int argc, char** argv) {
    ProbeOptions options;
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

ProbeSummary probeFile(const fs::path& path) {
    const auto data = readFile(path);
    if (!hasDirectorContainerHeader(data)) {
        throw SkippedFile("not a Director container header: " + printableFourCC(data));
    }

    auto director_file = libreshockwave::DirectorFile::load(data);
    director_file->setBasePath(path.parent_path().string());

    const auto score = director_file->scoreChunk();
    const auto external_casts = director_file->getExternalCastPaths();

    return ProbeSummary{
        data.size(),
        director_file->chunks().size(),
        director_file->castMembers().size(),
        director_file->castMembers().size(),
        director_file->scripts().size(),
        director_file->palettes().size(),
        score == nullptr ? 0U : static_cast<std::size_t>(std::max(0, score->getChannelCount())),
        score == nullptr ? 0U : static_cast<std::size_t>(std::max(0, score->getFrameCount())),
        external_casts.size(),
        director_file->stageWidth(),
        director_file->stageHeight(),
        director_file->channelCount(),
        director_file->version(),
        director_file->isAfterburner(),
        libreshockwave::format::toString(director_file->movieType()),
    };
}

void printVerboseOk(const fs::path& path, const ProbeSummary& summary) {
    std::cout << "OK " << pathString(path)
              << " version=" << summary.version
              << " movie=" << summary.movie_type
              << " afterburner=" << (summary.afterburner ? "yes" : "no")
              << " stage=" << summary.stage_width << 'x' << summary.stage_height
              << " channels=" << summary.channels
              << " chunks=" << summary.chunks
              << " castMembers=" << summary.cast_members
              << " scannedMembers=" << summary.scanned_members
              << " scripts=" << summary.scripts
              << " palettes=" << summary.palettes
              << " score=" << summary.score_channels << " channels x " << summary.score_frames << " frames"
              << " externalCasts=" << summary.external_casts
              << '\n';
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

        std::cout << "Director probe: files=" << files.size() << '\n' << std::flush;

        std::size_t ok_count = 0;
        std::size_t skipped_count = 0;
        std::size_t failure_count = 0;
        std::size_t total_bytes = 0;
        std::size_t total_chunks = 0;
        std::size_t total_cast_members = 0;
        std::size_t total_scanned_members = 0;
        std::size_t total_scripts = 0;
        std::size_t total_score_channels = 0;
        std::size_t total_score_frames = 0;

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
                std::cout << "PROBE " << (file_index + 1) << '/' << files.size() << ' '
                          << pathString(file) << '\n' << std::flush;
            }

            try {
                const auto summary = probeFile(file);
                ++ok_count;
                total_bytes += summary.bytes;
                total_chunks += summary.chunks;
                total_cast_members += summary.cast_members;
                total_scanned_members += summary.scanned_members;
                total_scripts += summary.scripts;
                total_score_channels += summary.score_channels;
                total_score_frames += summary.score_frames;

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

        std::cout << "Director probe summary: ok=" << ok_count
                  << " skipped=" << skipped_count
                  << " failed=" << failure_count
                  << " bytes=" << total_bytes
                  << " chunks=" << total_chunks
                  << " castMembers=" << total_cast_members
                  << " scannedMembers=" << total_scanned_members
                  << " scripts=" << total_scripts
                  << " scoreChannels=" << total_score_channels
                  << " scoreFrames=" << total_score_frames
                  << '\n';

        if (ok_count == 0 && !files.empty() && failure_count == 0 && !options.allow_empty) {
            std::cerr << "No valid Director containers were loaded.\n";
            return 2;
        }
        return failure_count == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Director probe error: " << error.what() << '\n';
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }
}
