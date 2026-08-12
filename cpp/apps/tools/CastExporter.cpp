#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/audio/SoundConverter.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/cast/ShapeInfo.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/FontMapChunk.hpp"
#include "libreshockwave/chunks/MediaChunk.hpp"
#include "libreshockwave/chunks/RawChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/font/Pfr1Font.hpp"
#include "libreshockwave/font/Pfr1TtfConverter.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"

#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
#include <zlib.h>
#else
#error "libreshockwave_cast_exporter requires zlib. Build the library with zlib available."
#endif

namespace {

namespace fs = std::filesystem;

struct ExportOptions {
    bool verbose = false;
    bool decompileScripts = true;
    bool rawBin = false;
    int jobs = 1;
    std::set<std::string> typeFilter;
    std::vector<std::string> roots;
};

struct ExportStats {
    std::size_t bitmaps = 0;
    std::size_t sounds = 0;
    std::size_t scripts = 0;
    std::size_t texts = 0;
    std::size_t palettes = 0;
    std::size_t fonts = 0;
    std::size_t raw = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
};

std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string sanitizeFileName(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

std::string usage(const char* argv0) {
    std::ostringstream out;
    out << "Usage: " << argv0
        << " [--only bitmap,sound,script,text,palette,font,raw] [--no-decompile] [--bin]"
        << " [--jobs N] [--verbose]"
        << " <file-or-directory> <output-directory>\n"
        << "Exports every cast member from Director .cct/.cst/.dcr/.dir/.dxr files into a directory.\n"
        << "Members are sorted into per-type subfolders: bitmaps/, sounds/, scripts/, texts/, palettes/,\n"
        << "fonts/, shapes/, and other/.\n"
        << "Bitmaps become .png, sounds .mp3/.wav, scripts decompiled .ls (or .bin with --no-decompile),\n"
        << "text .txt, palettes .pal, fonts .ttf, and everything else raw .bin data.\n"
        << "Each movie also gets a movie.txt with its stage/config and a linked_casts.txt when it links casts.\n"
        << "--bin also keeps the raw .bin sidecars for shape/xtra/other members (default: readable .txt only).\n"
        << "--jobs N processes up to N movies concurrently (default 1).";
    return out.str();
}

std::vector<std::string> splitCommaList(std::string_view value) {
    std::vector<std::string> result;
    std::string current;
    for (const char ch : value) {
        if (ch == ',') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    return result;
}

ExportOptions parseOptions(int argc, char** argv) {
    ExportOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--help" || arg == "-h") {
            std::cout << usage(argv[0]) << '\n';
            std::exit(0);
        }
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
            continue;
        }
        if (arg == "--no-decompile") {
            options.decompileScripts = false;
            continue;
        }
        if (arg == "--bin") {
            options.rawBin = true;
            continue;
        }
        if (arg == "--jobs" || arg == "-j") {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string(arg) + " requires a value");
            }
            options.jobs = std::max(1, std::atoi(argv[++index]));
            continue;
        }
        constexpr std::string_view jobsPrefix = "--jobs=";
        if (arg.starts_with(jobsPrefix)) {
            options.jobs = std::max(1, std::atoi(arg.substr(jobsPrefix.size()).data()));
            continue;
        }
        constexpr std::string_view onlyPrefix = "--only=";
        if (arg.starts_with(onlyPrefix)) {
            const auto entries = splitCommaList(arg.substr(onlyPrefix.size()));
            options.typeFilter.insert(entries.begin(), entries.end());
            continue;
        }
        if (arg == "--only") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--only requires a comma-separated value");
            }
            const auto entries = splitCommaList(argv[++index]);
            options.typeFilter.insert(entries.begin(), entries.end());
            continue;
        }
        if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + std::string(arg));
        }
        options.roots.emplace_back(arg);
    }
    if (options.roots.size() != 2) {
        throw std::runtime_error("Expected an input file/directory and an output directory");
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

void writeFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("Unable to write output file: " + path.string());
    }
}

void writeTextFile(const fs::path& path, const std::string& text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    writeFile(path, bytes);
}

bool hasDirectorContainerHeader(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    const std::string_view header(reinterpret_cast<const char*>(data.data()), 4);
    return header == "RIFX" || header == "XFIR" || header == "RIFF" || header == "FFIR";
}

// --- Minimal PNG encoder --------------------------------------------------

std::uint32_t pngCrcTableEntry(std::uint32_t value) {
    for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
    }
    return value;
}

std::uint32_t pngCrc(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::size_t index = 0; index < entries.size(); ++index) {
            entries[index] = pngCrcTableEntry(static_cast<std::uint32_t>(index));
        }
        return entries;
    }();
    for (std::size_t index = 0; index < size; ++index) {
        crc = table[(crc ^ data[index]) & 0xFFU] ^ (crc >> 8U);
    }
    return crc;
}

void pngWrite32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void pngAppendChunk(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 4>& type,
                    const std::vector<std::uint8_t>& payload) {
    pngWrite32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), payload.begin(), payload.end());
    std::uint32_t crc = pngCrc(&out[start], out.size() - start, 0xFFFFFFFFU);
    pngWrite32(out, crc ^ 0xFFFFFFFFU);
}

std::vector<std::uint8_t> encodePng(const libreshockwave::bitmap::Bitmap& bitmap) {
    const int width = bitmap.width();
    const int height = bitmap.height();
    const auto& pixels = bitmap.pixels();

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1U + static_cast<std::size_t>(width) * 4U));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter: None
        for (int x = 0; x < width; ++x) {
            const std::uint32_t argb = pixels[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
            raw.push_back(static_cast<std::uint8_t>((argb >> 16U) & 0xFFU));
            raw.push_back(static_cast<std::uint8_t>((argb >> 8U) & 0xFFU));
            raw.push_back(static_cast<std::uint8_t>(argb & 0xFFU));
            raw.push_back(static_cast<std::uint8_t>((argb >> 24U) & 0xFFU));
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const int result = compress2(compressed.data(), &compressedSize, raw.data(),
                                 static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION);
    if (result != Z_OK) {
        throw std::runtime_error("PNG zlib compression failed");
    }
    compressed.resize(compressedSize);

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header(13);
    header[0] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 24U) & 0xFFU);
    header[1] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 16U) & 0xFFU);
    header[2] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 8U) & 0xFFU);
    header[3] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) & 0xFFU);
    header[4] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 24U) & 0xFFU);
    header[5] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 16U) & 0xFFU);
    header[6] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 8U) & 0xFFU);
    header[7] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) & 0xFFU);
    header[8] = 8; // bit depth
    header[9] = 6; // color type: RGBA
    header[10] = 0;
    header[11] = 0;
    header[12] = 0;
    pngAppendChunk(out, {'I', 'H', 'D', 'R'}, header);
    pngAppendChunk(out, {'I', 'D', 'A', 'T'}, compressed);
    pngAppendChunk(out, {'I', 'E', 'N', 'D'}, {});
    return out;
}

// --- Export helpers -------------------------------------------------------

std::string normalizeText(const std::string& source) {
    std::string text;
    text.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\r') {
            text.push_back('\n');
            if (index + 1U < source.size() && source[index + 1U] == '\n') {
                ++index;
            }
        } else {
            text.push_back(source[index]);
        }
    }
    return text;
}

std::string scriptTypeDisplayName(libreshockwave::chunks::ScriptChunkType type) {
    switch (type) {
        case libreshockwave::chunks::ScriptChunkType::Score:
            return "Score";
        case libreshockwave::chunks::ScriptChunkType::Behavior:
            return "Behavior";
        case libreshockwave::chunks::ScriptChunkType::MovieScript:
            return "Movie Script";
        case libreshockwave::chunks::ScriptChunkType::Parent:
            return "Parent";
        case libreshockwave::chunks::ScriptChunkType::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::string paletteExportText(const std::vector<std::uint32_t>& colors) {
    std::ostringstream out;
    out << "JASC-PAL\n0100\n" << colors.size() << "\n";
    for (const auto rgb : colors) {
        out << static_cast<int>((rgb >> 16U) & 0xFFU) << " "
            << static_cast<int>((rgb >> 8U) & 0xFFU) << " "
            << static_cast<int>(rgb & 0xFFU) << "\n";
    }
    return out.str();
}

struct MemberExport {
    std::string fileName;
    std::vector<std::uint8_t> bytes;
};

std::optional<MemberExport> exportBitmapPalette(const libreshockwave::bitmap::Bitmap& bitmap,
                                                const std::string& baseName) {
    if (bitmap.bitDepth() >= 32 || !bitmap.imagePalette()) {
        return std::nullopt;
    }
    const auto& colors = bitmap.imagePalette()->colors();
    if (colors.empty()) {
        return std::nullopt;
    }
    const auto text = paletteExportText(colors);
    return MemberExport{baseName + ".pal", std::vector<std::uint8_t>(text.begin(), text.end())};
}

std::optional<MemberExport> exportBitmapRegPoint(
    const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
    const std::string& baseName) {
    std::ostringstream out;
    out << "regX=" << member->regPointX() << "\n";
    out << "regY=" << member->regPointY() << "\n";
    const auto text = out.str();
    return MemberExport{baseName + ".regpoint", std::vector<std::uint8_t>(text.begin(), text.end())};
}

std::vector<MemberExport> exportSound(libreshockwave::DirectorFile& file,
                                      const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                      const std::string& baseName) {
    std::vector<MemberExport> results;
    int soundIndex = 0;
    for (const auto& chunk : file.getLinkedChunksForMember(member)) {
        std::shared_ptr<libreshockwave::chunks::SoundChunk> sound;
        if (const auto soundChunk = std::dynamic_pointer_cast<libreshockwave::chunks::SoundChunk>(chunk)) {
            sound = soundChunk;
        } else if (const auto media = std::dynamic_pointer_cast<libreshockwave::chunks::MediaChunk>(chunk)) {
            sound = std::make_shared<libreshockwave::chunks::SoundChunk>(media->toSoundChunk());
        }
        if (!sound) {
            continue;
        }
        ++soundIndex;
        std::string name = baseName;
        if (soundIndex > 1) {
            name += "_" + std::to_string(soundIndex);
        }
        if (sound->isMp3()) {
            auto mp3 = libreshockwave::audio::SoundConverter::extractMp3(*sound);
            if (mp3 && !mp3->empty()) {
                results.push_back(MemberExport{name + ".mp3", std::move(*mp3)});
            }
        } else {
            auto wav = libreshockwave::audio::SoundConverter::toWav(*sound);
            if (wav.size() > 44U) {
                results.push_back(MemberExport{name + ".wav", std::move(wav)});
            }
        }
    }
    return results;
}

std::optional<MemberExport> exportScript(libreshockwave::DirectorFile& file,
                                         const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                         const std::string& baseName,
                                         bool decompile) {
    const auto script = file.getScriptForCastMember(member);
    if (!script) {
        return std::nullopt;
    }
    if (!decompile) {
        return MemberExport{baseName + ".bin", script->rawBytecode()};
    }
    const auto names = file.getScriptNamesForScript(script);
    std::string source;
    try {
        libreshockwave::lingo::decompiler::LingoDecompiler decompiler;
        source = decompiler.decompile(*script, names.get());
    } catch (const std::exception& error) {
        std::ostringstream out;
        out << "-- Decompilation error: " << error.what() << "\n";
        out << "-- Member #" << member->scriptId() << "\n";
        source = out.str();
    }
    const std::string header = "-- Cast member: " + member->name() + "\n"
                               + "-- Type: " + scriptTypeDisplayName(script->resolvedScriptType()) + "\n\n";
    std::string text = header + source;
    return MemberExport{baseName + ".ls", std::vector<std::uint8_t>(text.begin(), text.end())};
}

std::optional<MemberExport> exportText(libreshockwave::DirectorFile& file,
                                       const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                       const std::string& baseName) {
    const auto textChunk = file.getTextForMember(member);
    if (!textChunk) {
        return std::nullopt;
    }
    const auto text = normalizeText(textChunk->text());
    return MemberExport{baseName + ".txt", std::vector<std::uint8_t>(text.begin(), text.end())};
}

std::optional<MemberExport> exportPalette(libreshockwave::DirectorFile& file,
                                          const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                          const std::string& baseName) {
    int paletteIndex = 0;
    bool found = false;
    for (const auto& candidate : file.castMembers()) {
        if (candidate == member) {
            found = true;
            break;
        }
        if (candidate && candidate->memberType() == libreshockwave::cast::MemberType::Palette) {
            ++paletteIndex;
        }
    }
    if (!found) {
        return std::nullopt;
    }

    std::shared_ptr<const libreshockwave::bitmap::Palette> palette =
        file.resolvePaletteByMemberNumber(paletteIndex + 1);
    if (!palette) {
        return std::nullopt;
    }
    const auto text = paletteExportText(palette->colors());
    return MemberExport{baseName + ".pal", std::vector<std::uint8_t>(text.begin(), text.end())};
}

std::optional<MemberExport> exportFont(libreshockwave::DirectorFile& file,
                                       const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                       const std::string& baseName) {
    for (const auto& chunk : file.getLinkedChunksForMember(
             member, libreshockwave::format::fourCC(libreshockwave::format::ChunkType::XMED))) {
        const auto raw = std::dynamic_pointer_cast<libreshockwave::chunks::RawChunk>(chunk);
        if (!raw || raw->data().size() < 4) {
            continue;
        }
        const auto& data = raw->data();
        if (data[0] != 'P' || data[1] != 'F' || data[2] != 'R' || data[3] != '1') {
            continue;
        }
        try {
            const auto font = libreshockwave::font::Pfr1Font::parse(data);
            const auto ttf = libreshockwave::font::Pfr1TtfConverter::convert(*font, font->fontName);
            return MemberExport{baseName + ".ttf", ttf};
        } catch (const std::exception&) {
            continue;
        }
    }
    return std::nullopt;
}

std::optional<MemberExport> exportRaw(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                      const std::string& baseName) {
    const auto& data = member->specificData();
    if (data.empty()) {
        return std::nullopt;
    }
    return MemberExport{baseName + ".bin", data};
}

std::vector<MemberExport> exportRawStrings(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                                           const std::string& baseName) {
    const auto& data = member->specificData();
    std::ostringstream text;
    std::size_t currentOffset = 0;
    std::size_t runOffset = 0;
    std::string current;
    const auto flush = [&]() {
        if (current.size() >= 3) {
            text << std::hex << std::setw(4) << std::setfill('0') << runOffset << std::dec
                 << ": " << current << '\n';
        }
        current.clear();
    };
    for (std::size_t index = 0; index < data.size(); ++index) {
        const auto byte = data[index];
        if (byte >= 0x20 && byte <= 0x7E) {
            if (current.empty()) {
                runOffset = currentOffset;
            }
            current.push_back(static_cast<char>(byte));
        } else {
            flush();
        }
        ++currentOffset;
    }
    flush();
    const auto textString = text.str();
    if (textString.empty()) {
        return {};
    }
    return {MemberExport{baseName + ".txt", std::vector<std::uint8_t>(textString.begin(), textString.end())}};
}

std::string_view shapeTypeName(libreshockwave::cast::ShapeType type) {
    switch (type) {
        case libreshockwave::cast::ShapeType::Rect:
            return "rect";
        case libreshockwave::cast::ShapeType::OvalRect:
            return "ovalRect";
        case libreshockwave::cast::ShapeType::Oval:
            return "oval";
        case libreshockwave::cast::ShapeType::Line:
            return "line";
        case libreshockwave::cast::ShapeType::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::optional<MemberExport> exportShapeText(
    const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
    const std::string& baseName) {
    const auto& data = member->specificData();
    if (data.empty()) {
        return std::nullopt;
    }
    libreshockwave::cast::ShapeInfo info;
    try {
        info = libreshockwave::cast::ShapeInfo::parse(data);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    std::ostringstream out;
    out << "shapeType: " << shapeTypeName(info.shapeType) << '\n';
    out << "regX: " << info.regX << '\n';
    out << "regY: " << info.regY << '\n';
    out << "width: " << info.width << '\n';
    out << "height: " << info.height << '\n';
    out << std::hex << std::showbase;
    out << "color: " << (info.color & 0xFFFFFF) << '\n';
    out << "backColor: " << (info.backColor & 0xFFFFFF) << '\n';
    out << std::dec << std::noshowbase;
    out << "fillType: " << info.fillType << '\n';
    out << "lineThickness: " << info.lineThickness << '\n';
    out << "lineDirection: " << info.lineDirection << '\n';
    out << "filled: " << (info.isFilled() ? "yes" : "no") << '\n';
    out << "outlineInvisible: " << (info.isOutlineInvisible() ? "yes" : "no") << '\n';
    const auto text = out.str();
    return MemberExport{baseName + ".txt", std::vector<std::uint8_t>(text.begin(), text.end())};
}
bool wantsFontPass(const ExportOptions& options) {
    if (options.typeFilter.empty()) {
        return true;
    }
    return options.typeFilter.contains("font") || options.typeFilter.contains("fonts");
}

void exportMovieFonts(libreshockwave::DirectorFile& file,
                      const fs::path& movieDir,
                      const ExportOptions& options,
                      ExportStats& stats) {
    if (!wantsFontPass(options)) {
        return;
    }

    const fs::path fontsDir = movieDir / "fonts";
    std::set<int> exportedChunkIds;
    int fontOrdinal = 0;
    for (const auto& member : file.castMembers()) {
        if (!member || member->memberType() == libreshockwave::cast::MemberType::Font) {
            continue;
        }
        for (const auto& chunk : file.getLinkedChunksForMember(
                 member, libreshockwave::format::fourCC(libreshockwave::format::ChunkType::XMED))) {
            const auto raw = std::dynamic_pointer_cast<libreshockwave::chunks::RawChunk>(chunk);
            if (!raw || raw->data().size() < 4) {
                continue;
            }
            const auto& data = raw->data();
            if (data[0] != 'P' || data[1] != 'F' || data[2] != 'R' || data[3] != '1') {
                continue;
            }
            if (!exportedChunkIds.insert(raw->id().value()).second) {
                continue;
            }
            try {
                const auto font = libreshockwave::font::Pfr1Font::parse(data);
                const auto ttf = libreshockwave::font::Pfr1TtfConverter::convert(*font, font->fontName);
                auto safeName = sanitizeFileName(font->fontName);
                if (safeName.empty()) {
                    safeName = "font_" + std::to_string(raw->id().value());
                }
                ++fontOrdinal;
                std::ostringstream name;
                name << std::setw(4) << std::setfill('0') << fontOrdinal << "_" << safeName << ".ttf";
                writeFile(fontsDir / name.str(), ttf);
                ++stats.fonts;
                if (options.verbose) {
                    std::cout << "  wrote fonts/" << name.str() << '\n';
                }
            } catch (const std::exception&) {
                continue;
            }
        }
    }

    std::ostringstream manifest;
    for (const auto& map : file.fontMaps()) {
        if (!map) {
            continue;
        }
        for (const auto& entry : map->entries()) {
            manifest << entry.fontId << '\t' << entry.platform << '\t' << entry.fontName << '\n';
        }
    }
    const auto manifestText = manifest.str();
    if (!manifestText.empty()) {
        writeTextFile(movieDir / "fonts.txt", manifestText);
    }
}

void exportLinkedCasts(libreshockwave::DirectorFile& file, const fs::path& movieDir) {
    const auto paths = file.getExternalCastPaths();
    if (paths.empty()) {
        return;
    }
    std::ostringstream manifest;
    manifest << "# External casts linked from this movie, one per line.\n";
    std::set<std::string> seen;
    for (const auto& path : paths) {
        if (!seen.insert(path).second) {
            continue;
        }
        manifest << path << '\n';
    }
    writeTextFile(movieDir / "linked_casts.txt", manifest.str());
}

std::string colorHex(int value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(6) << std::setfill('0')
        << (value & 0xFFFFFF);
    return out.str();
}

void exportMovieConfig(libreshockwave::DirectorFile& file, const fs::path& movieDir) {
    const auto config = file.config();
    if (!config) {
        return;
    }
    std::ostringstream out;
    out << "stage_width\t" << config->stageWidth() << '\n';
    out << "stage_height\t" << config->stageHeight() << '\n';
    out << "stage_left\t" << config->stageLeft() << '\n';
    out << "stage_top\t" << config->stageTop() << '\n';
    out << "stage_right\t" << config->stageRight() << '\n';
    out << "stage_bottom\t" << config->stageBottom() << '\n';
    out << "background_color\t" << colorHex(config->bgColor()) << '\n';
    out << "stage_color\t" << colorHex(config->stageColor()) << '\n';
    out << "stage_color_rgb\t" << colorHex(config->stageColorRGB()) << '\n';
    out << "tempo\t" << config->tempo() << '\n';
    out << "min_member\t" << config->minMember() << '\n';
    out << "max_member\t" << config->maxMember() << '\n';
    out << "default_palette\t" << config->defaultPaletteCastLib() << ':' << config->defaultPaletteMember() << '\n';
    out << "director_version\t" << config->directorVersion() << '\n';
    out << "movie_version\t" << config->movieVersion() << '\n';
    out << "platform\t" << config->platform() << '\n';
    writeTextFile(movieDir / "movie.txt", out.str());
}

void exportCastList(libreshockwave::DirectorFile& file, const fs::path& movieDir) {
    const auto castList = file.castList();
    if (!castList || castList->entries().empty()) {
        return;
    }
    std::ostringstream out;
    out << "# Cast libraries in this movie. path is empty for internal casts; "
           "member_count 0 means an empty cast.\n";
    out << "id\tname\tpath\tmin_member\tmax_member\tmember_count\n";
    for (const auto& entry : castList->entries()) {
        out << entry.id << '\t'
            << entry.name << '\t'
            << entry.path << '\t'
            << entry.minMember << '\t'
            << entry.maxMember << '\t'
            << entry.memberCount << '\n';
    }
    writeTextFile(movieDir / "casts.txt", out.str());
}

std::string_view memberFolderName(libreshockwave::cast::MemberType type) {
    using libreshockwave::cast::MemberType;
    switch (type) {
        case MemberType::Bitmap:
        case MemberType::Picture:
            return "bitmaps";
        case MemberType::Sound:
            return "sounds";
        case MemberType::Script:
            return "scripts";
        case MemberType::Text:
        case MemberType::RichText:
        case MemberType::Button:
            return "texts";
        case MemberType::Palette:
            return "palettes";
        case MemberType::Font:
            return "fonts";
        case MemberType::Shape:
            return "shapes";
        default:
            return "other";
    }
}

std::string baseNameForMember(int ordinal, const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    std::ostringstream prefix;
    prefix << std::setw(4) << std::setfill('0') << ordinal << "_";
    const auto typeName = std::string{libreshockwave::cast::name(member->memberType())};
    auto safeName = sanitizeFileName(member->name());
    if (safeName.empty()) {
        safeName = "member_" + std::to_string(member->id().value());
    }
    return prefix.str() + typeName + "_" + safeName;
}

bool wantsType(const ExportOptions& options, libreshockwave::cast::MemberType type) {
    if (options.typeFilter.empty()) {
        return true;
    }
    const std::string_view typeName = libreshockwave::cast::name(type);
    return options.typeFilter.contains(std::string(typeName)) ||
           options.typeFilter.contains(std::string(typeName) + "s");
}

void exportMember(libreshockwave::DirectorFile& file,
                  const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                  int ordinal,
                  const fs::path& outputDir,
                  const ExportOptions& options,
                  ExportStats& stats) {
    const auto type = member->memberType();
    if (!wantsType(options, type)) {
        ++stats.skipped;
        return;
    }
    const auto baseName = baseNameForMember(ordinal, member);
    const fs::path memberDir = outputDir / memberFolderName(type);
    std::vector<MemberExport> exports;

    switch (type) {
        case libreshockwave::cast::MemberType::Bitmap:
        case libreshockwave::cast::MemberType::Picture: {
            auto decoded = file.decodeBitmap(member);
            if (decoded) {
                std::fprintf(stderr, "[paldbg] member %s chunk=%d pal=%s idx254=0x%06X\n",
                             baseName.c_str(), member->id().value(),
                             decoded->imagePalette() ? decoded->imagePalette()->name().c_str() : "none",
                             decoded->imagePalette() ? (decoded->imagePalette()->getColor(254) & 0xFFFFFFU) : 0);
                exports.push_back(MemberExport{baseName + ".png", encodePng(*decoded)});
                if (const auto palette = exportBitmapPalette(*decoded, baseName)) {
                    exports.push_back(*palette);
                }
            }
            break;
        }
        case libreshockwave::cast::MemberType::Sound: {
            exports = exportSound(file, member, baseName);
            break;
        }
        case libreshockwave::cast::MemberType::Script: {
            if (const auto data = exportScript(file, member, baseName, options.decompileScripts)) {
                exports.push_back(*data);
            }
            break;
        }
        case libreshockwave::cast::MemberType::Text:
        case libreshockwave::cast::MemberType::RichText:
        case libreshockwave::cast::MemberType::Button: {
            if (const auto data = exportText(file, member, baseName)) {
                exports.push_back(*data);
            }
            break;
        }
        case libreshockwave::cast::MemberType::Palette: {
            if (const auto data = exportPalette(file, member, baseName)) {
                exports.push_back(*data);
            }
            break;
        }
        case libreshockwave::cast::MemberType::Font: {
            if (const auto data = exportFont(file, member, baseName)) {
                exports.push_back(*data);
            }
            break;
        }
        case libreshockwave::cast::MemberType::Shape: {
            if (options.rawBin) {
                if (const auto data = exportRaw(member, baseName)) {
                    exports.push_back(*data);
                }
            }
            if (const auto shapeText = exportShapeText(member, baseName)) {
                exports.push_back(*shapeText);
            }
            break;
        }
        default: {
            if (options.rawBin) {
                if (const auto data = exportRaw(member, baseName)) {
                    exports.push_back(*data);
                }
            }
            const auto stringExports = exportRawStrings(member, baseName);
            exports.insert(exports.end(), stringExports.begin(), stringExports.end());
            break;
        }
    }

    if (exports.empty()) {
        ++stats.skipped;
        if (options.verbose) {
            std::cout << "  skip " << baseName << '\n';
        }
        return;
    }

    for (const auto& exportData : exports) {
        const fs::path path = memberDir / exportData.fileName;
        try {
            writeFile(path, exportData.bytes);
        } catch (const std::exception& error) {
            ++stats.failed;
            std::cerr << "  FAIL " << exportData.fileName << ": " << error.what() << '\n';
        }
        if (options.verbose) {
            std::cout << "  wrote " << exportData.fileName << " (" << exportData.bytes.size() << " bytes)\n";
        }
    }

    if (type == libreshockwave::cast::MemberType::Bitmap ||
        type == libreshockwave::cast::MemberType::Picture) {
        if (const auto regPoint = exportBitmapRegPoint(member, baseName)) {
            try {
                writeFile(memberDir / regPoint->fileName, regPoint->bytes);
            } catch (const std::exception& error) {
                ++stats.failed;
                std::cerr << "  FAIL " << regPoint->fileName << ": " << error.what() << '\n';
            }
        }
    }

    if (type == libreshockwave::cast::MemberType::Bitmap ||
        type == libreshockwave::cast::MemberType::Picture) {
        ++stats.bitmaps;
    } else if (type == libreshockwave::cast::MemberType::Sound) {
        ++stats.sounds;
    } else if (type == libreshockwave::cast::MemberType::Script) {
        ++stats.scripts;
    } else if (type == libreshockwave::cast::MemberType::Text ||
               type == libreshockwave::cast::MemberType::RichText ||
               type == libreshockwave::cast::MemberType::Button) {
        ++stats.texts;
    } else if (type == libreshockwave::cast::MemberType::Palette) {
        ++stats.palettes;
    } else if (type == libreshockwave::cast::MemberType::Font) {
        ++stats.fonts;
    } else {
        ++stats.raw;
    }
}

ExportStats exportOneFile(const fs::path& file,
                          bool inputIsDirectory,
                          const fs::path& inputRoot,
                          const fs::path& outputDir,
                          const ExportOptions& options,
                          std::mutex& logMutex) {
    ExportStats stats;
    std::unique_lock<std::mutex> logLock(logMutex);
    std::cout << "EXPORT " << file.string() << '\n' << std::flush;
    logLock.unlock();
    try {
        const auto data = readFile(file);
        if (!hasDirectorContainerHeader(data)) {
            ++stats.skipped;
            std::cout << "  skip (not a Director container)\n";
            return stats;
        }
        auto directorFile = libreshockwave::DirectorFile::load(data);
        directorFile->setBasePath(file.parent_path().string());

        fs::path movieDir = outputDir;
        if (inputIsDirectory) {
            std::error_code relEc;
            movieDir = movieDir / fs::relative(file, inputRoot, relEc).parent_path();
        }
        movieDir = movieDir / file.stem();
        fs::create_directories(movieDir);

        int ordinal = 0;
        for (const auto& member : directorFile->castMembers()) {
            ++ordinal;
            try {
                exportMember(*directorFile, member, ordinal, movieDir, options, stats);
            } catch (const std::exception& error) {
                ++stats.failed;
                std::cerr << "  FAIL " << member->name() << ": " << error.what() << '\n';
            }
        }
        try {
            exportMovieFonts(*directorFile, movieDir, options, stats);
        } catch (const std::exception& error) {
            ++stats.failed;
            std::cerr << "  FAIL fonts: " << error.what() << '\n';
        }
        try {
            exportLinkedCasts(*directorFile, movieDir);
        } catch (const std::exception& error) {
            ++stats.failed;
            std::cerr << "  FAIL linked casts: " << error.what() << '\n';
        }
        try {
            exportMovieConfig(*directorFile, movieDir);
        } catch (const std::exception& error) {
            ++stats.failed;
            std::cerr << "  FAIL config: " << error.what() << '\n';
        }
        try {
            exportCastList(*directorFile, movieDir);
        } catch (const std::exception& error) {
            ++stats.failed;
            std::cerr << "  FAIL cast list: " << error.what() << '\n';
        }
    } catch (const std::exception& error) {
        ++stats.failed;
        std::cerr << "  FAIL load: " << error.what() << '\n';
    }
    if (options.verbose) {
        std::cout << "  summary: bitmaps=" << stats.bitmaps
                  << " sounds=" << stats.sounds
                  << " scripts=" << stats.scripts
                  << " texts=" << stats.texts
                  << " palettes=" << stats.palettes
                  << " fonts=" << stats.fonts
                  << " raw=" << stats.raw
                  << " skipped=" << stats.skipped
                  << " failed=" << stats.failed
                  << '\n';
    }
    return stats;
}

int run(int argc, char** argv) {
    const auto options = parseOptions(argc, argv);
    const fs::path inputRoot(options.roots[0]);
    const fs::path outputDir(options.roots[1]);

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_regular_file(inputRoot, ec)) {
        files.push_back(inputRoot);
    } else if (fs::is_directory(inputRoot, ec)) {
        fs::recursive_directory_iterator iterator(inputRoot, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        while (!ec && iterator != end) {
            std::error_code entryEc;
            const auto path = iterator->path();
            if (iterator->is_regular_file(entryEc) && !entryEc) {
                const auto extension = toLower(path.extension().string());
                if (extension == ".cct" || extension == ".cst" || extension == ".dcr" ||
                    extension == ".dir" || extension == ".dxr") {
                    files.push_back(path);
                }
            }
            iterator.increment(ec);
        }
    } else {
        throw std::runtime_error("Input path does not exist: " + inputRoot.string());
    }
    if (ec) {
        throw std::runtime_error("Unable to scan input path: " + ec.message());
    }
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());

    fs::create_directories(outputDir);
    const bool inputIsDirectory = fs::is_directory(inputRoot, ec);

    ExportStats totals;
    if (options.jobs > 1 && files.size() > 1) {
        std::atomic<std::size_t> next{0};
        std::mutex logMutex;
        std::mutex totalsMutex;
        std::vector<std::thread> threads;
        threads.reserve(options.jobs);
        for (int i = 0; i < options.jobs; ++i) {
            threads.emplace_back([&]() {
                while (true) {
                    const std::size_t index = next.fetch_add(1);
                    if (index >= files.size()) {
                        return;
                    }
                    const auto stats = exportOneFile(
                        files[index], inputIsDirectory, inputRoot, outputDir, options, logMutex);
                    std::lock_guard<std::mutex> totalsLock(totalsMutex);
                    totals.bitmaps += stats.bitmaps;
                    totals.sounds += stats.sounds;
                    totals.scripts += stats.scripts;
                    totals.texts += stats.texts;
                    totals.palettes += stats.palettes;
                    totals.fonts += stats.fonts;
                    totals.raw += stats.raw;
                    totals.skipped += stats.skipped;
                    totals.failed += stats.failed;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        std::mutex logMutex;
        for (const auto& file : files) {
            const auto stats = exportOneFile(
                file, inputIsDirectory, inputRoot, outputDir, options, logMutex);
            totals.bitmaps += stats.bitmaps;
            totals.sounds += stats.sounds;
            totals.scripts += stats.scripts;
            totals.texts += stats.texts;
            totals.palettes += stats.palettes;
            totals.fonts += stats.fonts;
            totals.raw += stats.raw;
            totals.skipped += stats.skipped;
            totals.failed += stats.failed;
        }
    }

    std::cout << "Cast exporter summary: files=" << files.size()
              << " bitmaps=" << totals.bitmaps
              << " sounds=" << totals.sounds
              << " scripts=" << totals.scripts
              << " texts=" << totals.texts
              << " palettes=" << totals.palettes
              << " fonts=" << totals.fonts
              << " raw=" << totals.raw
              << " skipped=" << totals.skipped
              << " failed=" << totals.failed
              << " output=" << outputDir.string()
              << '\n';
    return totals.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Cast exporter error: " << error.what() << '\n';
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }
}
