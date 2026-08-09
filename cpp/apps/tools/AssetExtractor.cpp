#include <algorithm>
#include <array>
#include <cctype>
#include <csetjmp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef LIBRESHOCKWAVE_HAVE_JPEG
#include <jpeglib.h>
#endif

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/audio/SoundConverter.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/BitmapChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/Chunk.hpp"
#include "libreshockwave/chunks/KeyTableChunk.hpp"
#include "libreshockwave/chunks/MediaChunk.hpp"
#include "libreshockwave/chunks/PaletteChunk.hpp"
#include "libreshockwave/chunks/RawChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"

namespace fs = std::filesystem;

namespace {

using libreshockwave::DirectorFile;
using libreshockwave::bitmap::Bitmap;
using libreshockwave::chunks::BitmapChunk;
using libreshockwave::chunks::CastMemberChunk;
using libreshockwave::chunks::Chunk;
using libreshockwave::chunks::KeyTableChunk;
using libreshockwave::chunks::MediaChunk;
using libreshockwave::chunks::RawChunk;
using libreshockwave::chunks::ScriptChunk;
using libreshockwave::chunks::SoundChunk;
using libreshockwave::chunks::TextChunk;

struct FileCounts {
    int members = 0;
    int png = 0;
    int text = 0;
    int sounds = 0;
    int palettes = 0;
    int raw = 0;
    int scripts = 0;
    int errors = 0;
};

[[nodiscard]] std::string exceptionText() {
    try {
        throw;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown exception";
    }
}

[[nodiscard]] std::string clean(std::string_view value) {
    std::string result;
    result.reserve(std::min<std::size_t>(value.size(), 120));
    bool inInvalidRun = false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '.' || byte == '_' || byte == '-') {
            result.push_back(character);
            inInvalidRun = false;
        } else if (!inInvalidRun) {
            result.push_back('_');
            inInvalidRun = true;
        }
    }
    if (result.empty()) {
        return "unnamed";
    }
    if (result.size() > 120) {
        result.resize(120);
    }
    return result;
}

[[nodiscard]] std::string safeCell(std::string_view value) {
    std::string result(value);
    for (auto& character : result) {
        if (character == '\t' || character == '\n' || character == '\r') {
            character = ' ';
        }
    }
    return result;
}

[[nodiscard]] bool isBlank(std::string_view value) {
    return value.empty() || std::all_of(value.begin(), value.end(), [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] std::string memberStem(const CastMemberChunk& member) {
    const auto& name = member.name();
    const auto value = isBlank(name) ? std::string_view("member") : std::string_view(name);
    std::ostringstream result;
    result << std::setw(5) << std::setfill('0') << member.id().value() << '_' << clean(value);
    return result.str();
}

[[nodiscard]] std::shared_ptr<CastMemberChunk> scriptOwner(
    DirectorFile& file,
    const std::shared_ptr<ScriptChunk>& script) {
    if (!script) {
        return nullptr;
    }

    if (const auto keyTable = file.keyTable(); keyTable != nullptr) {
        if (const auto ownerId = keyTable->getOwnerCastId(script->id()); ownerId.has_value()) {
            for (const auto& member : file.castMembers()) {
                if (member && member->id().value() == ownerId->value() && member->isScript()) {
                    return member;
                }
            }
        }
    }

    for (const auto& member : file.castMembers()) {
        if (!member || !member->isScript()) {
            continue;
        }
        for (const auto& candidate : file.getScriptsByContextId(member->scriptId())) {
            if (candidate && candidate->id().value() == script->id().value()) {
                return member;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::string scriptStem(
    DirectorFile& file,
    const std::shared_ptr<ScriptChunk>& script,
    const std::shared_ptr<CastMemberChunk>& owner) {
    if (owner) {
        return memberStem(*owner);
    }

    const auto name = file.getScriptName(script);
    std::ostringstream result;
    result << std::setw(5) << std::setfill('0') << script->id().value() << '_'
           << clean(isBlank(name) ? std::string_view("script") : std::string_view(name));
    return result.str();
}

[[nodiscard]] std::string scriptAssembly(
    const ScriptChunk& script,
    const libreshockwave::chunks::ScriptNamesChunk* names,
    const libreshockwave::lingo::decompiler::LingoDecompiler& decompiler) {
    std::ostringstream output;
    output << "-- " << libreshockwave::format::getScriptTypeName(script.resolvedScriptType()) << '\n';
    for (std::size_t index = 0; index < script.handlers().size(); ++index) {
        if (index > 0) {
            output << '\n';
        }
        output << decompiler.formatHandlerBytecodeOnly(script.handlers()[index], names);
    }
    return output.str();
}

[[nodiscard]] std::string row(const CastMemberChunk& member,
                              std::string_view chunkId,
                              std::string_view fourcc,
                              std::string_view assetPath) {
    std::ostringstream result;
    result << member.id().value() << '\t'
           << libreshockwave::cast::name(member.memberType()) << '\t'
           << safeCell(member.name()) << '\t'
           << member.scriptId() << '\t'
           << safeCell(chunkId) << '\t'
           << safeCell(fourcc) << '\t'
           << safeCell(assetPath);
    return result.str();
}

[[nodiscard]] std::string relativeAssetPath(const fs::path& output, const fs::path& asset) {
    return asset.lexically_relative(output).generic_string();
}

void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    if (!data.empty()) {
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }
    if (!output) {
        throw std::runtime_error("Unable to write output file: " + path.string());
    }
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("Unable to write output file: " + path.string());
    }
}

void appendU32BE(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return ~crc;
}

[[nodiscard]] std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t modulus = 65521U;
    std::uint32_t sumA = 1U;
    std::uint32_t sumB = 0U;
    for (const auto byte : data) {
        sumA = (sumA + byte) % modulus;
        sumB = (sumB + sumA) % modulus;
    }
    return (sumB << 16U) | sumA;
}

void appendPngChunk(std::vector<std::uint8_t>& png,
                    std::array<std::uint8_t, 4> type,
                    const std::vector<std::uint8_t>& data) {
    if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("PNG chunk is too large");
    }
    appendU32BE(png, static_cast<std::uint32_t>(data.size()));
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());

    std::vector<std::uint8_t> crcInput;
    crcInput.reserve(type.size() + data.size());
    crcInput.insert(crcInput.end(), type.begin(), type.end());
    crcInput.insert(crcInput.end(), data.begin(), data.end());
    appendU32BE(png, crc32(crcInput));
}

[[nodiscard]] std::vector<std::uint8_t> pngBytes(const Bitmap& bitmap) {
    const auto width = bitmap.width();
    const auto height = bitmap.height();
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Cannot write a PNG with empty dimensions");
    }

    const auto widthSize = static_cast<std::size_t>(width);
    const auto heightSize = static_cast<std::size_t>(height);
    if (widthSize > (std::numeric_limits<std::size_t>::max() - 1U) / 4U ||
        heightSize > std::numeric_limits<std::size_t>::max() /
                         (1U + widthSize * 4U)) {
        throw std::runtime_error("Bitmap is too large to write as PNG");
    }

    const auto rowSize = 1U + widthSize * 4U;
    std::vector<std::uint8_t> filtered;
    filtered.reserve(heightSize * rowSize);
    const auto& pixels = bitmap.pixels();
    if (pixels.size() != widthSize * heightSize) {
        throw std::runtime_error("Bitmap pixel data does not match its dimensions");
    }
    for (std::size_t y = 0; y < heightSize; ++y) {
        filtered.push_back(0); // PNG filter: None
        for (std::size_t x = 0; x < widthSize; ++x) {
            const auto pixel = pixels[y * widthSize + x];
            filtered.push_back(static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU));
            filtered.push_back(static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU));
            filtered.push_back(static_cast<std::uint8_t>(pixel & 0xFFU));
            filtered.push_back(static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU));
        }
    }

    // A stored-block DEFLATE stream keeps this writer independent of a PNG
    // library while remaining valid for every PNG reader.
    std::vector<std::uint8_t> compressed{0x78, 0x01};
    std::size_t offset = 0;
    do {
        const auto remaining = filtered.size() - offset;
        const auto blockSize = std::min<std::size_t>(remaining, 65535U);
        const bool finalBlock = offset + blockSize == filtered.size();
        compressed.push_back(finalBlock ? 1U : 0U);
        const auto length = static_cast<std::uint16_t>(blockSize);
        compressed.push_back(static_cast<std::uint8_t>(length & 0xFFU));
        compressed.push_back(static_cast<std::uint8_t>((length >> 8U) & 0xFFU));
        const auto inverse = static_cast<std::uint16_t>(~length);
        compressed.push_back(static_cast<std::uint8_t>(inverse & 0xFFU));
        compressed.push_back(static_cast<std::uint8_t>((inverse >> 8U) & 0xFFU));
        compressed.insert(compressed.end(),
                          filtered.begin() + static_cast<std::ptrdiff_t>(offset),
                          filtered.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    } while (offset < filtered.size());
    appendU32BE(compressed, adler32(filtered));

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    header.reserve(13);
    appendU32BE(header, static_cast<std::uint32_t>(width));
    appendU32BE(header, static_cast<std::uint32_t>(height));
    header.push_back(8); // bit depth
    header.push_back(6); // RGBA
    header.push_back(0); // compression method
    header.push_back(0); // filter method
    header.push_back(0); // no interlace
    appendPngChunk(png, {'I', 'H', 'D', 'R'}, header);
    appendPngChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendPngChunk(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}

void writePng(const fs::path& path, const Bitmap& bitmap) {
    writeBytes(path, pngBytes(bitmap));
}

#ifdef LIBRESHOCKWAVE_HAVE_JPEG
struct JpegErrorManager {
    jpeg_error_mgr publicError;
    std::jmp_buf jump;
};

void jpegErrorExit(j_common_ptr common) {
    auto* error = reinterpret_cast<JpegErrorManager*>(common->err);
    longjmp(error->jump, 1);
}

[[nodiscard]] std::optional<Bitmap> decodeJpeg(const std::vector<std::uint8_t>& data) {
    if (data.empty() || data.size() > std::numeric_limits<unsigned long>::max()) {
        return std::nullopt;
    }

    jpeg_decompress_struct decoder{};
    JpegErrorManager error{};
    bool created = false;
    decoder.err = jpeg_std_error(&error.publicError);
    error.publicError.error_exit = jpegErrorExit;
    if (setjmp(error.jump) != 0) {
        if (created) {
            jpeg_destroy_decompress(&decoder);
        }
        return std::nullopt;
    }

    jpeg_create_decompress(&decoder);
    created = true;
    jpeg_mem_src(&decoder,
                 const_cast<unsigned char*>(data.data()),
                 static_cast<unsigned long>(data.size()));
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&decoder);
        return std::nullopt;
    }
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);

    const auto width = static_cast<int>(decoder.output_width);
    const auto height = static_cast<int>(decoder.output_height);
    if (width <= 0 || height <= 0 || decoder.output_components != 3) {
        jpeg_destroy_decompress(&decoder);
        return std::nullopt;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
    std::vector<JSAMPLE> scanline(static_cast<std::size_t>(width) * 3U);
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = scanline.data();
        const auto y = decoder.output_scanline;
        jpeg_read_scanlines(&decoder, &row, 1);
        for (int x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(x) * 3U;
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] =
                0xFF000000U |
                (static_cast<std::uint32_t>(scanline[offset]) << 16U) |
                (static_cast<std::uint32_t>(scanline[offset + 1U]) << 8U) |
                static_cast<std::uint32_t>(scanline[offset + 2U]);
        }
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return Bitmap(width, height, 32, std::move(pixels));
}
#endif

[[nodiscard]] std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open input file: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("Unable to determine input size: " + path.string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!input && !input.eof()) {
        throw std::runtime_error("Unable to read input file: " + path.string());
    }
    return data;
}

[[nodiscard]] bool isDirectorFile(const fs::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".dcr" || extension == ".cct" || extension == ".cst" ||
           extension == ".dir" || extension == ".dxr";
}

[[nodiscard]] std::string baseName(const fs::path& path) {
    const auto filename = path.filename().string();
    const auto dot = filename.find_last_of('.');
    return clean(dot != std::string::npos && dot > 0
                     ? std::string_view(filename).substr(0, dot)
                     : std::string_view(filename));
}

[[nodiscard]] std::vector<KeyTableChunk::KeyTableEntry> entries(
    const DirectorFile& file,
    const CastMemberChunk& member) {
    const auto keyTable = file.keyTable();
    return keyTable ? keyTable->getEntriesForOwner(member.id())
                    : std::vector<KeyTableChunk::KeyTableEntry>{};
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> rawBytes(
    const std::shared_ptr<Chunk>& chunk) {
    if (const auto raw = std::dynamic_pointer_cast<RawChunk>(chunk)) {
        return raw->data();
    }
    if (const auto bitmap = std::dynamic_pointer_cast<BitmapChunk>(chunk)) {
        return bitmap->data();
    }
    if (const auto sound = std::dynamic_pointer_cast<SoundChunk>(chunk)) {
        return sound->audioData();
    }
    if (const auto media = std::dynamic_pointer_cast<MediaChunk>(chunk)) {
        return media->audioData();
    }
    if (const auto text = std::dynamic_pointer_cast<TextChunk>(chunk)) {
        const auto& value = text->text();
        return std::vector<std::uint8_t>(value.begin(), value.end());
    }
    return std::nullopt;
}

void writeFileInfo(const DirectorFile& file, const fs::path& input, const fs::path& output) {
    std::ostringstream info;
    info << "source\t" << input.string() << '\n'
         << "afterburner\t" << (file.isAfterburner() ? "true" : "false") << '\n'
         << "version\t" << file.version() << '\n'
         << "movie_type\t" << libreshockwave::format::toString(file.movieType()) << '\n'
         << "stage_width\t" << file.stageWidth() << '\n'
         << "stage_height\t" << file.stageHeight() << '\n'
         << "tempo\t" << file.tempo() << '\n'
         << "cast_members\t" << file.castMembers().size() << '\n'
         << "scripts\t" << file.scripts().size() << '\n'
         << "palettes\t" << file.palettes().size() << '\n';
    writeText(output / "file_info.tsv", info.str());
}

void logError(std::vector<std::string>& errors,
              const fs::path& input,
              const CastMemberChunk* member,
              std::string_view phase) {
    std::ostringstream line;
    line << input.filename().string() << '\t'
         << (member == nullptr ? "" : std::to_string(member->id().value())) << '\t'
         << phase << '\t' << exceptionText();
    errors.push_back(line.str());
}

[[nodiscard]] FileCounts extractFile(const fs::path& input, const fs::path& output) {
    FileCounts counts;
    std::vector<std::string> manifest{
        "member_id\tmember_type\tmember_name\tscript_id\tchunk_id\tfourcc\tasset_path"};
    std::vector<std::string> errorLog;

    try {
        auto file = DirectorFile::load(readBytes(input));
        file->setBasePath(input.parent_path().string());
        writeFileInfo(*file, input, output);

        const auto bitmapDir = output / "bitmaps";
        const auto textDir = output / "text";
        const auto soundDir = output / "sounds";
        const auto paletteDir = output / "palettes";
        const auto rawDir = output / "raw_chunks";
        const auto scriptDir = output / "scripts";
        fs::create_directories(bitmapDir);
        fs::create_directories(textDir);
        fs::create_directories(soundDir);
        fs::create_directories(paletteDir);
        fs::create_directories(rawDir);
        fs::create_directories(scriptDir);

        for (const auto& script : file->scripts()) {
            if (!script) {
                continue;
            }
            std::shared_ptr<CastMemberChunk> owner;
            try {
                owner = scriptOwner(*file, script);
                const auto names = file->getScriptNamesForScript(script);
                libreshockwave::lingo::decompiler::LingoDecompiler decompiler;
                const auto stem = scriptStem(*file, script, owner);
                const auto sourceAsset = scriptDir / (stem + ".ls");
                const auto assemblyAsset = scriptDir / (stem + ".lsasm");
                writeText(sourceAsset, decompiler.decompile(*script, names.get()));
                writeText(assemblyAsset, scriptAssembly(*script, names.get(), decompiler));

                if (owner) {
                    const auto chunkId = std::to_string(script->id().value());
                    manifest.push_back(row(*owner, chunkId, "LS", relativeAssetPath(output, sourceAsset)));
                    manifest.push_back(row(*owner, chunkId, "LSASM", relativeAssetPath(output, assemblyAsset)));
                }
            } catch (...) {
                ++counts.errors;
                logError(errorLog, input, owner.get(), "script-export");
            }
        }

        for (const auto& member : file->castMembers()) {
            const auto stem = memberStem(*member);

            if (member->isBitmap()) {
                try {
                    const auto bitmap = file->decodeBitmap(member);
                    if (bitmap.has_value()) {
                        const auto asset = bitmapDir / (stem + ".png");
                        writePng(asset, *bitmap);
                        manifest.push_back(row(*member, "", "", relativeAssetPath(output, asset)));
                        ++counts.png;
                    }
                } catch (...) {
                    ++counts.errors;
                    logError(errorLog, input, member.get(), "bitmap");
                }
            }

            try {
                const auto textChunks = file->getTextChunksForMember(member);
                for (std::size_t index = 0; index < textChunks.size(); ++index) {
                    const auto& text = textChunks[index];
                    const auto asset = textDir / (stem + "_stxt_" + std::to_string(index) + ".txt");
                    writeText(asset, text->text());
                    manifest.push_back(row(*member,
                                           std::to_string(text->id().value()),
                                           "STXT",
                                           relativeAssetPath(output, asset)));
                    ++counts.text;
                }
                if (member->isTextXtra()) {
                    const auto xmed = file->getXmedStyledTextForMember(member);
                    if (xmed.has_value()) {
                        const auto asset = textDir / (stem + "_xmed.txt");
                        writeText(asset, xmed->text);
                        manifest.push_back(row(*member, "", "XMED", relativeAssetPath(output, asset)));
                        ++counts.text;
                    }
                }
            } catch (...) {
                ++counts.errors;
                logError(errorLog, input, member.get(), "text");
            }

            if (member->isSound()) {
                try {
                    for (const auto& entry : entries(*file, *member)) {
                        const auto chunk = file->getChunk(entry.sectionId);
                        std::optional<SoundChunk> convertedSound;
                        const SoundChunk* sound = nullptr;
                        if (const auto direct = std::dynamic_pointer_cast<SoundChunk>(chunk)) {
                            sound = direct.get();
                        } else if (const auto media = std::dynamic_pointer_cast<MediaChunk>(chunk)) {
                            convertedSound = media->toSoundChunk();
                            sound = &*convertedSound;
                        }
                        if (sound == nullptr) {
                            continue;
                        }

                        std::vector<std::uint8_t> data;
                        std::string extension;
                        if (sound->isMp3()) {
                            const auto mp3 = libreshockwave::audio::SoundConverter::extractMp3(*sound);
                            if (!mp3.has_value()) {
                                continue;
                            }
                            data = *mp3;
                            extension = ".mp3";
                        } else {
                            data = libreshockwave::audio::SoundConverter::toWav(*sound);
                            extension = ".wav";
                        }
                        const auto asset = soundDir /
                            (stem + "_" + std::to_string(entry.sectionId.value()) + extension);
                        writeBytes(asset, data);
                        manifest.push_back(row(*member,
                                               std::to_string(entry.sectionId.value()),
                                               entry.fourccString(),
                                               relativeAssetPath(output, asset)));
                        ++counts.sounds;
                    }
                } catch (...) {
                    ++counts.errors;
                    logError(errorLog, input, member.get(), "sound");
                }
            }

            try {
                if (member->isScript() && file->getScriptByContextId(member->scriptId())) {
                    ++counts.scripts;
                }
            } catch (...) {
                ++counts.errors;
                logError(errorLog, input, member.get(), "script-count");
            }

            try {
                for (const auto& entry : entries(*file, *member)) {
                    const auto data = rawBytes(file->getChunk(entry.sectionId));
                    if (!data.has_value()) {
                        continue;
                    }
                    const auto asset = rawDir /
                        (stem + "_" + std::to_string(entry.sectionId.value()) + "_" +
                         clean(entry.fourccString()) + ".bin");
                    writeBytes(asset, *data);
                    manifest.push_back(row(*member,
                                           std::to_string(entry.sectionId.value()),
                                           entry.fourccString(),
                                           relativeAssetPath(output, asset)));
                    ++counts.raw;
                }
            } catch (...) {
                ++counts.errors;
                logError(errorLog, input, member.get(), "raw");
            }
        }

        for (const auto& palette : file->palettes()) {
            try {
                const auto asset = output / "palettes" /
                    ("palette_" + std::to_string(palette->id().value()) + ".tsv");
                std::ostringstream table;
                table << "index\tr\tg\tb\thex\n";
                for (int index = 0; index < palette->colorCount(); ++index) {
                    const auto rgb = palette->getColor(index);
                    table << index << '\t'
                          << ((rgb >> 16U) & 0xFFU) << '\t'
                          << ((rgb >> 8U) & 0xFFU) << '\t'
                          << (rgb & 0xFFU) << '\t' << '#'
                          << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
                          << (rgb & 0xFFFFFFU) << std::dec << std::nouppercase
                          << std::setfill(' ') << '\n';
                }
                writeText(asset, table.str());
                ++counts.palettes;
            } catch (...) {
                ++counts.errors;
                logError(errorLog, input, nullptr, "palette");
            }
        }

        std::ostringstream manifestText;
        for (const auto& line : manifest) {
            manifestText << line << '\n';
        }
        writeText(output / "manifest.tsv", manifestText.str());
        if (!errorLog.empty()) {
            std::ostringstream errors;
            for (const auto& line : errorLog) {
                errors << line << '\n';
            }
            writeText(output / "errors.log", errors.str());
        }
        counts.members = static_cast<int>(file->castMembers().size());
        return counts;
    } catch (...) {
        try {
            writeText(output / "errors.log", exceptionText() + "\n");
        } catch (...) {
            // Keep the extraction failure as a per-file error if logging fails.
        }
        ++counts.errors;
        return counts;
    }
}

void printUsage(std::ostream& output) {
    output << "usage: libreshockwave_asset_extractor <input-dir> [output-dir]\n"
           << "Extract Director/Shockwave files (.dcr, .cct, .cst, .dir, .dxr) into the\n"
           << "manifest.tsv/file_info.tsv/bitmaps/text/sounds/palettes/raw_chunks format.\n"
           << "If output-dir is omitted, the generated files are written into input-dir.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (std::string_view(argv[1]) == "-h" ||
                      std::string_view(argv[1]) == "--help")) {
        printUsage(std::cout);
        return 0;
    }
    if (argc < 2 || argc > 3) {
        printUsage(std::cerr);
        return 2;
    }

    const fs::path inputDir = argv[1];
    const fs::path outputDir = argc == 3 ? fs::path(argv[2]) : inputDir;
    try {
        if (!fs::is_directory(inputDir)) {
            throw std::runtime_error("Input directory does not exist or is not a directory: " +
                                     inputDir.string());
        }
        fs::create_directories(outputDir);

#ifdef LIBRESHOCKWAVE_HAVE_JPEG
        DirectorFile::setJpegDecoder(decodeJpeg);
#endif

        std::vector<fs::path> inputs;
        for (const auto& entry : fs::directory_iterator(inputDir)) {
            if (entry.is_regular_file() && isDirectorFile(entry.path())) {
                inputs.push_back(entry.path());
            }
        }
        std::sort(inputs.begin(), inputs.end(), [](const fs::path& left, const fs::path& right) {
            return left.filename().string() < right.filename().string();
        });

        std::vector<std::string> summary{
            "file\tmembers\tpng\ttext\tsounds\tpalettes\traw\tscripts\tterrors"};
        int cleanFiles = 0;
        int failedFiles = 0;
        for (const auto& input : inputs) {
            const auto fileOutput = outputDir / baseName(input);
            fs::create_directories(fileOutput);
            const auto counts = extractFile(input, fileOutput);
            std::ostringstream line;
            line << input.filename().string() << '\t'
                 << counts.members << '\t' << counts.png << '\t' << counts.text << '\t'
                 << counts.sounds << '\t' << counts.palettes << '\t' << counts.raw << '\t'
                 << counts.scripts << '\t' << counts.errors;
            summary.push_back(line.str());
            if (counts.errors == 0) {
                ++cleanFiles;
            } else {
                ++failedFiles;
            }
            std::cout << input.filename().string()
                      << ": members=" << counts.members
                      << " png=" << counts.png
                      << " text=" << counts.text
                      << " sounds=" << counts.sounds
                      << " palettes=" << counts.palettes
                      << " raw=" << counts.raw
                      << " scripts=" << counts.scripts
                      << " errors=" << counts.errors << '\n';
        }

        std::ostringstream summaryText;
        for (const auto& line : summary) {
            summaryText << line << '\n';
        }
        writeText(outputDir / "libreshockwave_summary.tsv", summaryText.str());
        std::cout << "Processed " << inputs.size() << " files ("
                  << cleanFiles << " clean, " << failedFiles << " with extraction errors)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset extraction error: " << error.what() << '\n';
        return 1;
    }
}
