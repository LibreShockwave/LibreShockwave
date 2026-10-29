#include "libreshockwave/player/cast/cast_lib.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/bitmap_decoder.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/key_table_chunk.hpp"
#include "libreshockwave/chunks/raw_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/chunks/sound_chunk.hpp"
#include "libreshockwave/chunks/text_chunk.hpp"
#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/player/audio/sound_manager.hpp"
#include "libreshockwave/player/bitmap_resolver.hpp"
#include "libreshockwave/player/cast/font_registry.hpp"
#include "libreshockwave/util/file_util.hpp"

namespace libreshockwave::player::cast {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isSystemMacPalette(const std::shared_ptr<const bitmap::Palette>& palette) {
    if (!palette) {
        return false;
    }
    const auto name = lower(palette->name());
    return palette.get() == &bitmap::Palette::systemMacPalette() ||
           name == "system - mac" || name == "systemmac";
}

std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lower(lhs) == lower(rhs);
}

bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

lingo::Datum stringDatum(const std::string& value) {
    return lingo::Datum::of(value);
}

void appendU32LE(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    data.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    data.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    data.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void putU16BE(std::vector<std::uint8_t>& data, int offset, int value) {
    const auto raw = static_cast<std::uint16_t>(value);
    data[static_cast<std::size_t>(offset)] = static_cast<std::uint8_t>((raw >> 8U) & 0xFFU);
    data[static_cast<std::size_t>(offset + 1)] = static_cast<std::uint8_t>(raw & 0xFFU);
}

int directorBitmapPitch(int width, int bit_depth) {
    if (bit_depth == 32) {
        return std::max(1, width) * 4;
    }
    return bitmap::BitmapDecoder::calculateScanWidth(std::max(1, width), bit_depth);
}

int directorPaletteId(const bitmap::Bitmap& bitmap) {
    if (bitmap.paletteRefMemberNum() > 0) {
        return bitmap.paletteRefMemberNum();
    }
    if (const auto& name = bitmap.paletteRefSystemName()) {
        const auto normalized = bitmap::Palette::normalizeBuiltInSymbolName(*name);
        if (normalized == "grayscale") {
            return bitmap::Palette::GRAYSCALE + 1;
        }
        if (normalized == "rainbow") {
            return bitmap::Palette::RAINBOW + 1;
        }
        if (normalized == "systemMac") {
            return bitmap::Palette::SYSTEM_MAC + 1;
        }
        if (normalized == "systemWin") {
            return bitmap::Palette::SYSTEM_WIN + 1;
        }
    }
    if (bitmap.bitDepth() <= 8 && bitmap.imagePalette() != nullptr) {
        const auto palette_name = lower(bitmap.imagePalette()->name());
        if (bitmap.imagePalette().get() == &bitmap::Palette::systemWinPalette() ||
            palette_name == lower(bitmap::Palette::systemWinPalette().name())) {
            return bitmap::Palette::SYSTEM_WIN + 1;
        }
        if (bitmap.imagePalette().get() == &bitmap::Palette::systemMacPalette() ||
            palette_name == lower(bitmap::Palette::systemMacPalette().name())) {
            return bitmap::Palette::SYSTEM_MAC + 1;
        }
    }
    if (bitmap.bitDepth() <= 8 && bitmap.imagePalette() != nullptr &&
        bitmap.imagePalette()->name() == bitmap::Palette::grayscalePalette().name()) {
        return bitmap::Palette::GRAYSCALE + 1;
    }
    return 0;
}

std::vector<std::uint8_t> bitmapPixelsAsDirectorBitd(const bitmap::Bitmap& bitmap, int pitch) {
    const int width = std::max(1, bitmap.width());
    const int height = std::max(1, bitmap.height());
    const int bit_depth = bitmap.bitDepth() <= 8 ? 8 : 32;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(pitch * height), 0);

    if (bit_depth == 8) {
        const auto indices = bitmap.paletteIndices();
        const auto* palette = bitmap.imagePalette().get();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto offset = static_cast<std::size_t>(y * pitch + x);
                if (indices.has_value() && static_cast<std::size_t>(y * width + x) < indices->size()) {
                    result[offset] = (*indices)[static_cast<std::size_t>(y * width + x)];
                } else if (palette != nullptr) {
                    result[offset] = static_cast<std::uint8_t>(palette->nearestIndex(bitmap.getPixel(x, y)) & 0xFF);
                } else {
                    const auto pixel = bitmap.getPixel(x, y);
                    const int r = static_cast<int>((pixel >> 16U) & 0xFFU);
                    const int g = static_cast<int>((pixel >> 8U) & 0xFFU);
                    const int b = static_cast<int>(pixel & 0xFFU);
                    result[offset] = static_cast<std::uint8_t>((r + g + b) / 3);
                }
            }
        }
        return result;
    }

    result.assign(static_cast<std::size_t>(pitch * height), 0);
    const int scan_width = pitch / 4;
    for (int y = 0; y < height; ++y) {
        const int line_offset = y * scan_width * 4;
        for (int x = 0; x < width; ++x) {
            const auto pixel = bitmap.getPixel(x, y);
            result[static_cast<std::size_t>(line_offset + x)] = static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
            result[static_cast<std::size_t>(line_offset + scan_width + x)] = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
            result[static_cast<std::size_t>(line_offset + scan_width * 2 + x)] = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
            result[static_cast<std::size_t>(line_offset + scan_width * 3 + x)] = static_cast<std::uint8_t>(pixel & 0xFFU);
        }
    }
    return result;
}

std::vector<std::uint8_t> encodeDirectorBitmapMedia(const bitmap::Bitmap& bitmap, int reg_x, int reg_y) {
    const int width = std::max(1, bitmap.width());
    const int height = std::max(1, bitmap.height());
    const int bit_depth = bitmap.bitDepth() <= 8 ? 8 : 32;
    const int pitch = directorBitmapPitch(width, bit_depth);

    std::vector<std::uint8_t> media(32, 0);
    putU16BE(media, 0, 0x8000 | pitch);
    putU16BE(media, 2, 0);
    putU16BE(media, 4, 0);
    putU16BE(media, 6, height);
    putU16BE(media, 8, width);
    putU16BE(media, 18, reg_y);
    putU16BE(media, 20, reg_x);
    media[22] = bitmap.isNativeAlpha() || bitmap.hasTransparentPixels() || bitmap.hasTranslucentPixels() ? 0x10 : 0;
    media[23] = static_cast<std::uint8_t>(bit_depth);
    putU16BE(media, 24, bitmap.paletteRefCastLib() > 0 ? bitmap.paletteRefCastLib() : 0);
    putU16BE(media, 26, bit_depth <= 8 ? directorPaletteId(bitmap) : 0);

    const auto bitd = bitmapPixelsAsDirectorBitd(bitmap, pitch);
    media.insert(media.end(), {'D', 'T', 'I', 'B'});
    appendU32LE(media, static_cast<std::uint32_t>(bitd.size()));
    media.insert(media.end(), bitd.begin(), bitd.end());
    return media;
}

std::string shapeTypeSymbolName(libreshockwave::cast::ShapeType type) {
    switch (type) {
        case libreshockwave::cast::ShapeType::Rect: return "rect";
        case libreshockwave::cast::ShapeType::OvalRect: return "roundRect";
        case libreshockwave::cast::ShapeType::Oval: return "oval";
        case libreshockwave::cast::ShapeType::Line: return "line";
        case libreshockwave::cast::ShapeType::Unknown: return "unknown";
    }
    return "unknown";
}

std::optional<libreshockwave::cast::ShapeType> shapeTypeFromDatum(const lingo::Datum& value) {
    using libreshockwave::cast::ShapeType;

    if (value.isString() || value.isSymbol()) {
        std::string name = lower(trim(value.stringValue()));
        if (!name.empty() && name.front() == '#') {
            name.erase(name.begin());
        }
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || std::isspace(ch);
        }), name.end());

        if (name == "rect" || name == "rectangle") return ShapeType::Rect;
        if (name == "roundrect" || name == "roundedrect" || name == "ovalrect") return ShapeType::OvalRect;
        if (name == "oval" || name == "ellipse") return ShapeType::Oval;
        if (name == "line") return ShapeType::Line;
        if (!value.isString()) {
            return std::nullopt;
        }
    }

    if (value.isNumber() || value.isString()) {
        const auto type = libreshockwave::cast::shapeTypeFromCode(value.intValue());
        if (type != ShapeType::Unknown) {
            return type;
        }
    }
    return std::nullopt;
}

int textBoxTypeFromDatum(const lingo::Datum& value) {
    if (value.isString() || value.isSymbol()) {
        std::string name = lower(trim(value.stringValue()));
        if (!name.empty() && name.front() == '#') {
            name.erase(name.begin());
        }
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || std::isspace(ch);
        }), name.end());

        if (name == "adjust" || name == "auto" || name == "autosize") return 0;
        if (name == "fixed") return 1;
        if (name == "scroll" || name == "scrolling") return 2;
        if (name == "limit") return 3;
        if (!value.isString()) {
            throw lingo::LingoException("Cannot convert symbol to text box type");
        }
    }

    return value.intValue();
}

std::string directorMemberTypeName(const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (!member || member->memberType() == libreshockwave::cast::MemberType::Null) {
        return "empty";
    }
    const auto type = member->memberType();
    if (type == libreshockwave::cast::MemberType::Text ||
        type == libreshockwave::cast::MemberType::Button ||
        (type == libreshockwave::cast::MemberType::Xtra &&
         member->rawChunk() != nullptr &&
         member->rawChunk()->isTextXtra())) {
        return "field";
    }
    return std::string(libreshockwave::cast::name(type));
}

std::string scriptChunkTypePropName(chunks::ScriptChunkType type) {
    switch (type) {
        case chunks::ScriptChunkType::Score: return "score";
        case chunks::ScriptChunkType::Behavior: return "behavior";
        case chunks::ScriptChunkType::MovieScript: return "movie_script";
        case chunks::ScriptChunkType::Parent: return "parent";
        case chunks::ScriptChunkType::Unknown: return "unknown";
    }
    return "unknown";
}

chunks::ScriptChunkType castMemberScriptTypeToScriptChunkType(chunks::CastMemberScriptType type) {
    switch (type) {
        case chunks::CastMemberScriptType::Score: return chunks::ScriptChunkType::Score;
        case chunks::CastMemberScriptType::Behavior: return chunks::ScriptChunkType::Behavior;
        case chunks::CastMemberScriptType::MovieScript: return chunks::ScriptChunkType::MovieScript;
        case chunks::CastMemberScriptType::Parent: return chunks::ScriptChunkType::Parent;
        case chunks::CastMemberScriptType::Unknown: return chunks::ScriptChunkType::Unknown;
    }
    return chunks::ScriptChunkType::Unknown;
}

lingo::Datum bitmapPaletteRefDatum(const std::shared_ptr<libreshockwave::cast::CastMember>& member,
                                   id::CastLibId cast_lib_id) {
    if (!member || !member->isBitmap()) {
        return lingo::Datum::voidValue();
    }

    if (member->paletteRefCastLib() >= 1 && member->paletteRefMemberNum() >= 1) {
        return lingo::Datum::castMemberRef(id::CastLibId(member->paletteRefCastLib()),
                                           id::MemberId(member->paletteRefMemberNum()));
    }
    if (const auto& system_name = member->paletteRefSystemName(); system_name.has_value()) {
        return lingo::Datum::symbol(*system_name);
    }

    const auto runtime = member->runtimeBitmap();
    if (runtime) {
        if (runtime->paletteRefCastLib() >= 1 && runtime->paletteRefMemberNum() >= 1) {
            return lingo::Datum::castMemberRef(id::CastLibId(runtime->paletteRefCastLib()),
                                               id::MemberId(runtime->paletteRefMemberNum()));
        }
        const auto& system_name = runtime->paletteRefSystemName();
        if (system_name.has_value()) {
            return lingo::Datum::symbol(*system_name);
        }
    }

    const auto& info = member->bitmapInfo();
    if (info.has_value()) {
        const auto built_in_symbol = bitmap::Palette::builtInSymbolName(info->palette_id);
        if (built_in_symbol.has_value() && info->palette_id != bitmap::Palette::SYSTEM_MAC) {
            return lingo::Datum::symbol(*built_in_symbol);
        }
        const int palette_member_number = info->palette_id + 1;
        if (palette_member_number >= 1) {
            return lingo::Datum::castMemberRef(cast_lib_id, id::MemberId(palette_member_number));
        }
    }

    return lingo::Datum::voidValue();
}

std::string normalizeTextLineEndings(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '\r') {
            result.push_back('\r');
            if (index + 1 < value.size() && value[index + 1] == '\n') {
                ++index;
            }
        } else if (ch == '\n') {
            result.push_back('\r');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::string pickTextLineDelimiter(std::string_view value) {
    if (value.find("\r\n") != std::string_view::npos) {
        return "\r\n";
    }
    if (value.find('\n') != std::string_view::npos) {
        return "\n";
    }
    if (value.find('\r') != std::string_view::npos) {
        return "\r";
    }
    return "\r\n";
}

std::vector<std::string> splitTextLines(std::string_view value) {
    if (value.empty()) {
        return {""};
    }

    const std::string delimiter = pickTextLineDelimiter(value);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t found = value.find(delimiter, start);
        if (found == std::string_view::npos) {
            lines.emplace_back(value.substr(start));
            break;
        }
        lines.emplace_back(value.substr(start, found - start));
        start = found + delimiter.size();
    }
    return lines;
}

std::string stripHtmlTags(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool in_tag = false;
    for (const char ch : value) {
        if (ch == '<') {
            in_tag = true;
            continue;
        }
        if (ch == '>' && in_tag) {
            in_tag = false;
            continue;
        }
        if (!in_tag) {
            result.push_back(ch);
        }
    }
    return result;
}

int textColorToArgb(const lingo::Datum& value) {
    if (const auto* color = value.asColorRef()) {
        return static_cast<int>(0xFF000000U |
                                ((static_cast<std::uint32_t>(color->r) & 0xFFU) << 16U) |
                                ((static_cast<std::uint32_t>(color->g) & 0xFFU) << 8U) |
                                (static_cast<std::uint32_t>(color->b) & 0xFFU));
    }
    if (value.isString()) {
        std::string text = trim(value.stringValue());
        if (text.size() >= 2) {
            const char first = text.front();
            const char last = text.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                text = trim(text.substr(1, text.size() - 2));
            }
        }
        if (!text.empty() && text.front() == '#') {
            text.erase(text.begin());
        }
        if (text.size() == 6) {
            try {
                const int rgb = std::stoi(text, nullptr, 16);
                return static_cast<int>(0xFF000000U | (static_cast<std::uint32_t>(rgb) & 0x00FFFFFFU));
            } catch (const std::exception&) {
                return static_cast<int>(0xFF000000U);
            }
        }
        return static_cast<int>(0xFF000000U);
    }
    const int raw = value.intValue();
    if (raw > 255) {
        return static_cast<int>(0xFF000000U | (static_cast<std::uint32_t>(raw) & 0x00FFFFFFU));
    }
    const int gray = 255 - std::clamp(raw, 0, 255);
    return static_cast<int>(0xFF000000U |
                            (static_cast<std::uint32_t>(gray) << 16U) |
                            (static_cast<std::uint32_t>(gray) << 8U) |
                            static_cast<std::uint32_t>(gray));
}

lingo::Datum colorDatumFromArgb(int argb) {
    return lingo::Datum::colorRef((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

void applyRuntimePaletteMetadata(bitmap::Bitmap& bitmap,
                                 const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (member == nullptr || !member->runtimePaletteOverride()) {
        return;
    }
    bitmap.setImagePalette(member->runtimePaletteOverride());
    if (member->paletteRefCastLib() >= 1 && member->paletteRefMemberNum() >= 1) {
        bitmap.setPaletteRefCastMember(member->paletteRefCastLib(), member->paletteRefMemberNum());
    } else if (member->paletteRefSystemName().has_value()) {
        bitmap.setPaletteRefSystemName(*member->paletteRefSystemName());
    }
}

lingo::Datum paletteColorListDatum(const bitmap::Palette& palette) {
    std::vector<lingo::Datum> colors;
    colors.reserve(static_cast<std::size_t>(palette.size()));
    for (int index = 0; index < palette.size(); ++index) {
        colors.push_back(colorDatumFromArgb(static_cast<int>(palette.getColor(index))));
    }
    return lingo::Datum::list(std::move(colors));
}

std::string fontStyleFromDatum(const lingo::Datum& value) {
    if (!value.isList()) {
        return value.stringValue();
    }
    std::string result;
    for (const auto& item : value.listValue().items()) {
        if (!result.empty()) {
            result += ',';
        }
        result += item.stringValue();
    }
    return result;
}

} // namespace

CastLib::CastLib(int number,
                 std::shared_ptr<chunks::CastChunk> cast_chunk,
                 const chunks::CastListChunk::CastListEntry* list_entry)
    : cast_lib_id_(number),
      selection_(lingo::Datum::list()),
      cast_chunk_(std::move(cast_chunk)) {
    if (list_entry != nullptr) {
        name_ = list_entry->name;
        file_name_ = list_entry->path;
        authored_file_name_ = file_name_;
        preload_mode_ = list_entry->preload_settings;
    }

    if (name_.empty() && number == 1) {
        name_ = "Internal";
    }
}

void CastLib::setSourceFile(std::shared_ptr<DirectorFile> file) {
    source_file_ = std::move(file);
}

void CastLib::setBitmapPaletteHostFile(std::shared_ptr<DirectorFile> file) {
    bitmap_palette_host_file_ = std::move(file);
}

std::shared_ptr<DirectorFile> CastLib::sourceFile() const {
    return source_file_;
}

void CastLib::reloadFromFile(std::shared_ptr<DirectorFile> file) {
    if (!file) {
        return;
    }
    source_file_ = std::move(file);
    state_ = State::None;
    member_chunks_.clear();
    members_.clear();
    invalidateMemberNameIndex();
    scripts_.clear();
    script_types_by_pointer_.clear();
    script_types_by_id_.clear();
    cached_scripts_.clear();
    total_slot_count_ = 0;
    load();
}

id::CastLibId CastLib::castLibId() const { return cast_lib_id_; }
int CastLib::number() const { return cast_lib_id_.value(); }
const std::string& CastLib::name() const { return name_; }

void CastLib::setName(std::string name) {
    name_ = std::move(name);
    name_explicitly_assigned_ = true;
    name_loaded_from_external_file_ = false;
}

const std::string& CastLib::fileName() const { return file_name_; }

void CastLib::setFileName(std::string file_name) {
    if (!sameFileBinding(file_name_, file_name)) {
        invalidateFileBackedBinding();
    }
    file_name_ = std::move(file_name);
    if (!name_explicitly_assigned_ && hasAuthoredExternalBinding() && !usesAuthoredExternalBinding()) {
        name_ = file_name_;
        name_loaded_from_external_file_ = false;
    }
}

bool CastLib::isExternal() const {
    return !file_name_.empty();
}

bool CastLib::isFetched() const {
    return source_file_ != nullptr || !fetched_external_data_.empty() || !isExternal();
}

bool CastLib::hasAuthoredExternalBinding() const {
    return !authored_file_name_.empty();
}

bool CastLib::matchesAuthoredExternalFile(const std::string& base_name) const {
    if (!hasAuthoredExternalBinding() || base_name.empty()) {
        return false;
    }
    const auto authored_base_name = util::getFileNameWithoutExtension(util::getFileName(authored_file_name_));
    return equalsIgnoreCase(authored_base_name, base_name);
}

bool CastLib::usesAuthoredExternalBinding() const {
    if (!hasAuthoredExternalBinding()) {
        return true;
    }
    if (file_name_.empty()) {
        return false;
    }
    const auto current_base_name = util::getFileNameWithoutExtension(util::getFileName(file_name_));
    return matchesAuthoredExternalFile(current_base_name);
}

bool CastLib::usesStableRegistryBinding() const {
    if (!hasAuthoredExternalBinding() || usesAuthoredExternalBinding()) {
        return true;
    }
    if (name_loaded_from_external_file_) {
        return false;
    }
    const auto runtime_name = trim(name_);
    if (runtime_name.empty() || usesGeneratedPlaceholderName(runtime_name)) {
        return false;
    }
    return !looksLikeDirectFileBindingName(runtime_name);
}

CastLib::State CastLib::state() const { return state_; }
bool CastLib::isLoaded() const { return state_ == State::Loaded; }
bool CastLib::isSettled() const {
    return state_ == State::Loaded || state_ == State::Failed;
}
bool CastLib::isFetching() const { return state_ == State::Fetching; }

void CastLib::markFetching() {
    if (state_ == State::None) {
        state_ = State::Fetching;
    }
}

int CastLib::preloadMode() const { return preload_mode_; }

void CastLib::setPreloadMode(int preload_mode) {
    preload_mode_ = preload_mode;
}

void CastLib::load() {
    if (state_ == State::Loaded || state_ == State::Failed) {
        return;
    }

    state_ = State::Loading;

    if (!source_file_) {
        if (!fetched_external_data_.empty() && setExternalData(fetched_external_data_)) {
            return;
        }
        state_ = isExternal() ? State::None : State::Loaded;
        return;
    }

    if (isExternal()) {
        if (!source_file_->casts().empty()) {
            loadFromExternalFile();
        }
        scanXmedFonts();
        state_ = State::Loaded;
        return;
    }

    if (!cast_chunk_ && !source_file_->casts().empty()) {
        loadFromExternalFile();
        scanXmedFonts();
        state_ = State::Loaded;
        return;
    }

    if (cast_chunk_) {
        loadMembersFromCast(cast_chunk_, minMember());
    }

    scanXmedFonts();
    state_ = State::Loaded;
}

int CastLib::memberCount() {
    if (!isLoaded()) {
        load();
    }
    return total_slot_count_ > 0 ? total_slot_count_ : static_cast<int>(member_chunks_.size());
}

const std::map<int, std::shared_ptr<chunks::CastMemberChunk>>& CastLib::memberChunks() {
    if (!isLoaded()) {
        load();
    }
    return member_chunks_;
}

const std::map<int, std::shared_ptr<libreshockwave::cast::CastMember>>& CastLib::runtimeMembers() {
    if (!isLoaded()) {
        load();
    }
    return members_;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberByNumber(int member_number) {
    if (!isLoaded()) {
        load();
    }
    if (const auto found = member_chunks_.find(member_number); found != member_chunks_.end()) {
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberByName(const std::string& member_name) {
    if (!isLoaded()) {
        load();
    }
    if (auto direct = findMemberChunkByNameExact(member_name)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(member_name); prefixed.has_value()) {
        return findMemberChunkByNameExact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getMember(int member_number) {
    if (!isLoaded()) {
        load();
    }
    if (const auto found = members_.find(member_number); found != members_.end()) {
        return found->second;
    }
    auto chunk = findMemberByNumber(member_number);
    if (!chunk) {
        return nullptr;
    }
    auto member = std::make_shared<libreshockwave::cast::CastMember>(
        chunk->id().value(), cast_lib_id_.value(), member_number, chunk);
    members_[member_number] = member;
    return member;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getMemberByName(const std::string& member_name) {
    if (!isLoaded()) {
        load();
    }
    if (auto direct = findMemberByNameExact(member_name)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(member_name); prefixed.has_value()) {
        return findMemberByNameExact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getPaletteMemberByName(
    const std::string& member_name) {
    if (!isLoaded()) {
        load();
    }
    if (member_name.empty()) {
        return nullptr;
    }

    auto find_exact = [this](const std::string& name) -> std::shared_ptr<libreshockwave::cast::CastMember> {
        for (const auto& [_, member] : members_) {
            if (member && member->isPalette() && equalsIgnoreCase(member->name(), name)) {
                return member;
            }
        }
        for (const auto& [number, chunk] : member_chunks_) {
            if (!chunk || !equalsIgnoreCase(chunk->name(), name)) {
                continue;
            }
            auto member = getMember(number);
            if (member && member->isPalette()) {
                return member;
            }
        }
        return nullptr;
    };

    if (auto direct = find_exact(member_name)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(member_name); prefixed.has_value()) {
        return find_exact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::createDynamicMember(const std::string& member_type) {
    if (!isLoaded()) {
        load();
    }
    const auto type = dynamicMemberTypeFor(member_type);
    for (int member_number = 10000; member_number < next_dynamic_member_; ++member_number) {
        if (member_chunks_.contains(member_number)) {
            continue;
        }
        if (const auto found = members_.find(member_number);
            found != members_.end() && found->second && found->second->isReusableDynamicSlot()) {
            found->second->reuseAs(type);
            invalidateMemberNameIndex();
            return found->second;
        }
    }
    const int member_number = nextAvailableDynamicMemberNumber();
    auto member = std::make_shared<libreshockwave::cast::CastMember>(
        cast_lib_id_.value(),
        member_number,
        type);
    members_[member_number] = member;
    invalidateMemberNameIndex();
    return member;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::createDynamicMember(
    const std::string& member_name,
    const std::string& member_type) {
    auto member = createDynamicMember(member_type);
    if (member) {
        member->setName(member_name);
        invalidateMemberNameIndex();
    }
    return member;
}

bool CastLib::hasMemberNamedExact(const std::string& member_name) {
    return findMemberChunkByNameExact(member_name) != nullptr || findMemberByNameExact(member_name) != nullptr;
}

bool CastLib::hasMemberNumber(int member_number) {
    if (!isLoaded()) {
        load();
    }
    if (member_chunks_.contains(member_number)) {
        return true;
    }
    const auto found = members_.find(member_number);
    return found != members_.end() && found->second != nullptr;
}

int CastLib::getMemberNumber(const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!isLoaded()) {
        load();
    }
    for (const auto& [number, existing] : member_chunks_) {
        if (sameAuthoredMember(existing, member)) {
            return number;
        }
    }
    return -1;
}

std::shared_ptr<chunks::ScriptChunk> CastLib::getScript(int member_number) {
    if (!isLoaded()) {
        load();
    }
    if (auto member = getMember(member_number); member && member->runtimeScript()) {
        return member->runtimeScript();
    }
    if (const auto found = scripts_.find(member_number); found != scripts_.end()) {
        return found->second;
    }
    return nullptr;
}

chunks::ScriptChunkType CastLib::scriptTypeForScript(const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return chunks::ScriptChunkType::Unknown;
    }
    if (!isLoaded()) {
        load();
    }

    if (const auto found = script_types_by_pointer_.find(script.get()); found != script_types_by_pointer_.end()) {
        return found->second;
    }
    if (const auto found = script_types_by_id_.find(script->id().value()); found != script_types_by_id_.end()) {
        script_types_by_pointer_[script.get()] = found->second;
        return found->second;
    }

    for (const auto& [member_number, candidate] : scripts_) {
        if (!candidate) {
            continue;
        }
        if (candidate.get() != script.get() && candidate->id().value() != script->id().value()) {
            continue;
        }
        if (const auto found = member_chunks_.find(member_number);
            found != member_chunks_.end() && found->second) {
            if (const auto member_type = found->second->getScriptType(); member_type.has_value()) {
                const auto script_type = castMemberScriptTypeToScriptChunkType(member_type.value());
                script_types_by_pointer_[script.get()] = script_type;
                script_types_by_id_[script->id().value()] = script_type;
                return script_type;
            }
        }
    }

    const auto script_type = script->scriptType();
    script_types_by_pointer_[script.get()] = script_type;
    script_types_by_id_[script->id().value()] = script_type;
    return script_type;
}

const std::vector<std::shared_ptr<chunks::ScriptChunk>>& CastLib::allScripts() {
    if (!isLoaded()) {
        load();
    }
    cached_scripts_.clear();
    if (source_file_) {
        cached_scripts_ = source_file_->scripts();
    } else {
        for (const auto& [_, script] : scripts_) {
            cached_scripts_.push_back(script);
        }
    }
    return cached_scripts_;
}

std::shared_ptr<chunks::ScriptNamesChunk> CastLib::scriptNames() const {
    return source_file_ ? source_file_->scriptNames() : nullptr;
}

lingo::Datum CastLib::getProp(const std::string& prop_name) {
    const auto prop = lower(prop_name);
    if (prop == "number") return lingo::Datum::of(cast_lib_id_.value());
    if (prop == "name") return stringDatum(name_);
    if (prop == "filename") return stringDatum(file_name_);
    if (prop == "preloadmode") return lingo::Datum::of(preload_mode_);
    if (prop == "selection") return selection_;
    if (prop == "loaded") return isLoaded() ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    if (prop.find("member") != std::string::npos) return lingo::Datum::of(memberCount());
    return lingo::Datum::voidValue();
}

bool CastLib::setProp(const std::string& prop_name, const lingo::Datum& value) {
    const auto prop = lower(prop_name);
    if (prop == "name") {
        setName(value.stringValue());
        return true;
    }
    if (prop == "filename") {
        setFileName(value.stringValue());
        return true;
    }
    if (prop == "preloadmode") {
        preload_mode_ = value.intValue();
        return true;
    }
    if (prop == "selection") {
        selection_ = value.isList() ? value : lingo::Datum::list();
        return true;
    }
    return false;
}

lingo::Datum CastLib::getMemberProp(int member_number, const std::string& prop_name) {
    auto member = getMember(member_number);
    if (!member) {
        return getInvalidMemberProp(prop_name);
    }

    const auto prop = lower(prop_name);
    if (prop == "name") return stringDatum(member->name());
    if (prop == "number") return lingo::Datum::of(id::SlotId::of(cast_lib_id_, id::MemberId(member_number)).value());
    if (prop == "membernum") return lingo::Datum::of(member_number);
    if (prop == "type") return lingo::Datum::symbol(directorMemberTypeName(member));
    if (prop == "castlibnum") return lingo::Datum::of(cast_lib_id_.value());
    if (prop == "castlib") return lingo::Datum::castLibRef(cast_lib_id_);
    if (prop == "script") {
        return getScript(member_number)
            ? lingo::Datum::scriptRef(lingo::Datum::CastMemberRef::of(cast_lib_id_, id::MemberId(member_number)))
            : lingo::Datum::voidValue();
    }
    if (prop == "scripttext") return stringDatum("");
    if (prop == "media") {
        if (member->isBitmap()) {
            auto bitmap = member->runtimeBitmap();
            if (!bitmap || member->hasRuntimeBitmapDecodePlaceholder() ||
                member->shouldRedecodeAuthoredRuntimeBitmap()) {
                const auto image = getMemberProp(member_number, "image");
                if (const auto* image_ref = image.asImageRef()) {
                    bitmap = image_ref->bitmap;
                }
            }
            if (bitmap) {
                auto media_bitmap = bitmap->copy();
                applyRuntimePaletteMetadata(media_bitmap, member);
                return lingo::Datum::media(encodeDirectorBitmapMedia(media_bitmap, member->regX(), member->regY()));
            }
        }
        return lingo::Datum::castMemberRef(cast_lib_id_, id::MemberId(member_number));
    }
    if (prop == "mediaready") return lingo::Datum::of(1);
    if (member->isSound() && prop == "duration") {
        if (source_file_ != nullptr && member->rawChunk()) {
            if (const auto sound = player::audio::SoundManager::findSoundForMember(*source_file_, member->rawChunk())) {
                return lingo::Datum::of(static_cast<int>(std::lround(sound->durationSeconds() * 1000.0)));
            }
        }
        return lingo::Datum::of(0);
    }
    if (member->isTextLike()) {
        if (prop == "text") return stringDatum(resolveMemberText(member));
        if (prop == "linecount") {
            const auto lines = splitTextLines(resolveMemberText(member));
            return lingo::Datum::of(static_cast<int>(lines.size()));
        }
        if (prop == "line") {
            std::vector<lingo::Datum> lines;
            const auto text_lines = splitTextLines(resolveMemberText(member));
            for (const auto& line : text_lines) {
                lines.push_back(stringDatum(line));
            }
            return lingo::Datum::list(std::move(lines));
        }
        if (prop == "width") return lingo::Datum::of(member->textRectRight() - member->textRectLeft());
        if (prop == "height") return lingo::Datum::of(member->textRectBottom() - member->textRectTop());
        if (prop == "rect") {
            return lingo::Datum::intRect(member->textRectLeft(),
                                         member->textRectTop(),
                                         member->textRectRight(),
                                         member->textRectBottom());
        }
        if (prop == "font") return stringDatum(member->textFont());
        if (prop == "fontsize") return lingo::Datum::of(member->textFontSize());
        if (prop == "fontstyle") return stringDatum(member->textFontStyle());
        if (prop == "alignment") return lingo::Datum::symbol(member->textAlignment());
        if (prop == "color") return colorDatumFromArgb(member->textColor());
        if (prop == "bgcolor") return colorDatumFromArgb(member->textBgColor());
        if (prop == "wordwrap") return lingo::Datum::of(member->textWordWrap() ? 1 : 0);
        if (prop == "antialias") return lingo::Datum::of(member->textAntialias() ? 1 : 0);
        if (prop == "boxtype") return lingo::Datum::of(member->textBoxType());
        if (prop == "fixedlinespace") return lingo::Datum::of(member->textFixedLineSpace());
        if (prop == "topspacing") return lingo::Datum::of(member->textTopSpacing());
        if (prop == "editable") return lingo::Datum::of(member->editable() ? 1 : 0);
    }
    if (member->isPalette() && prop == "color") {
        auto palette = member->paletteData();
        if (!palette && source_file_) {
            palette = source_file_->resolvePaletteByMemberNumber(member_number);
        }
        return palette ? paletteColorListDatum(*palette) : lingo::Datum::voidValue();
    }
    if (member->isBitmap() && (prop == "paletteref" || prop == "palette")) {
        return bitmapPaletteRefDatum(member, cast_lib_id_);
    }
    if (member->isShape()) {
        if (prop == "filled") return lingo::Datum::of(member->shapeFilled() ? 1 : 0);
        if (prop == "shapetype") return lingo::Datum::symbol(shapeTypeSymbolName(member->shapeType()));
        if (prop == "linesize") return lingo::Datum::of(member->shapeLineSize());
        if (prop == "pattern") return lingo::Datum::of(member->shapePattern());
    }
    if (member->isScript()) {
        if (prop == "text") return stringDatum("");
        if (prop == "scripttype") {
            auto script = getScript(member_number);
            return script ? stringDatum(scriptChunkTypePropName(script->scriptType())) : lingo::Datum::voidValue();
        }
    }
    if (prop == "width") return lingo::Datum::of(member->width());
    if (prop == "height") return lingo::Datum::of(member->height());
    if (prop == "depth") {
        auto bitmap = member->runtimeBitmap();
        if (bitmap) {
            return lingo::Datum::of(bitmap->bitDepth());
        }
        return lingo::Datum::of(member->bitmapInfo().has_value() ? member->bitmapInfo()->bit_depth : 0);
    }
    if (member->isBitmap() && prop == "alphathreshold") {
        return lingo::Datum::of(member->bitmapAlphaThreshold());
    }
    if (prop == "regpoint") return lingo::Datum::intPoint(member->regX(), member->regY());
    if (prop == "rect") return lingo::Datum::intRect(0, 0, member->width(), member->height());
    if (prop == "image") {
        auto bitmap = member->runtimeBitmap();
        const auto info = member->bitmapInfo();
        const bool uses_unlinked_palette = info.has_value() && info->palette_id >= 0 &&
            source_file_ != nullptr && source_file_->resolvePaletteExact(info->palette_id) == nullptr;
        const bool host_uses_windows_palette = bitmap_palette_host_file_ != nullptr &&
            bitmap_palette_host_file_->config() != nullptr && bitmap_palette_host_file_->config()->platform() == 2 &&
            uses_unlinked_palette;
        const bool runtime_needs_host_palette = host_uses_windows_palette && bitmap != nullptr &&
            isSystemMacPalette(bitmap->imagePalette()) && member->runtimePaletteOverride() == nullptr;
        if ((!bitmap || member->hasRuntimeBitmapDecodePlaceholder() ||
             member->shouldRedecodeAuthoredRuntimeBitmap() || runtime_needs_host_palette) &&
            member->isBitmap() &&
            member->rawChunk() &&
            source_file_) {
            const bool had_runtime_bitmap = bitmap != nullptr;
            const auto palette_override = member->runtimePaletteOverride();
            const bitmap::Palette* decode_palette = palette_override.get();
            if (decode_palette == nullptr && bitmap_palette_host_file_ &&
                bitmap_palette_host_file_->config() != nullptr &&
                bitmap_palette_host_file_->config()->platform() == 2 &&
                uses_unlinked_palette) {
                decode_palette = &bitmap::Palette::systemWinPalette();
            }
            std::optional<bitmap::Bitmap> decoded;
            if (bitmap_palette_host_file_ && source_file_.get() != bitmap_palette_host_file_.get()) {
                player::BitmapResolver resolver(bitmap_palette_host_file_, nullptr, nullptr);
                decoded = resolver.decodeBitmap(member->rawChunk(), decode_palette);
            } else {
                decoded = source_file_->decodeBitmap(member->rawChunk(), decode_palette);
            }
            if (decoded.has_value()) {
                applyRuntimePaletteMetadata(*decoded, member);
                member->setRuntimeBitmapFromAuthoredSource(*decoded);
                bitmap = member->runtimeBitmap();
            } else if (!had_runtime_bitmap &&
                       member->bitmapInfo().has_value() &&
                       member->bitmapInfo()->width > 0 &&
                       member->bitmapInfo()->height > 0) {
                bitmap::Bitmap placeholder(member->bitmapInfo()->width,
                                           member->bitmapInfo()->height,
                                           std::max(member->bitmapInfo()->bit_depth, 32));
                placeholder.setNativeAlpha(true);
                member->setRuntimeBitmapDecodePlaceholder(placeholder);
                bitmap = member->runtimeBitmap();
            }
        }
        if (!bitmap && member->isBitmap() && member->isRuntimeDynamic()) {
            bitmap::Bitmap default_bitmap(1, 1, 32);
            default_bitmap.fill(0xFFFFFFFFU);
            member->setRuntimeBitmap(default_bitmap, false);
            bitmap = member->runtimeBitmap();
        }
        if (!bitmap) {
            return lingo::Datum::voidValue();
        }
        std::weak_ptr<libreshockwave::cast::CastMember> weak_member(member);
        return lingo::Datum::imageRef(
            std::move(bitmap),
            [weak_member](bitmap::Bitmap&) {
                if (auto locked = weak_member.lock()) {
                    locked->syncRuntimeBitmapAnchorState();
                }
            });
    }
    return lingo::Datum::voidValue();
}

bool CastLib::setMemberProp(int member_number, const std::string& prop_name, const lingo::Datum& value) {
    auto member = getMember(member_number);
    if (!member) {
        return false;
    }

    const auto prop = lower(prop_name);
    if (prop == "name") {
        const std::string new_name = value.stringValue();
        if (new_name.empty() && member->isBitmap() && member->isRuntimeDynamic()) {
            // A released bitmap returns to the dynamic member pool. Its old
            // registration point belongs to the previous use, so it must not
            // override the registration point of the next media assignment.
            member->setRegPointState(member->regX(), member->regY(), false);
        }
        member->setName(new_name);
        invalidateMemberNameIndex();
        return true;
    }
    if (prop == "regpoint") {
        const auto* point = value.asIntPoint();
        if (point == nullptr) {
            return false;
        }
        member->setRegPoint(point->x, point->y);
        return true;
    }
    if (member->isTextLike()) {
        if (prop == "text") {
            member->setDynamicText(value.stringValue());
            return true;
        }
        if (prop == "html") {
            member->setDynamicText(stripHtmlTags(value.stringValue()));
            return true;
        }
        if (prop == "media" && (value.isString() || value.isSymbol())) {
            member->setDynamicText(value.stringValue());
            return true;
        }
        if (prop == "image") {
            const auto* image = value.asImageRef();
            if (image == nullptr || image->bitmap == nullptr) {
                return false;
            }
            member->setRuntimeBitmap(*image->bitmap);
            return true;
        }
        if (prop == "font") {
            member->setTextFont(value.stringValue());
            return true;
        }
        if (prop == "fontsize") {
            member->setTextFontSize(value.intValue());
            return true;
        }
        if (prop == "fontstyle") {
            member->setTextFontStyle(fontStyleFromDatum(value));
            return true;
        }
        if (prop == "alignment") {
            member->setTextAlignment(lower(value.stringValue()));
            return true;
        }
        if (prop == "color") {
            if (!value.isVoid()) {
                member->setTextColor(textColorToArgb(value));
            }
            return true;
        }
        if (prop == "bgcolor") {
            if (!value.isVoid()) {
                member->setTextBgColor(textColorToArgb(value));
            }
            return true;
        }
        if (prop == "wordwrap") {
            member->setTextWordWrap(value.intValue() != 0);
            return true;
        }
        if (prop == "antialias") {
            member->setTextAntialias(value.intValue() != 0);
            return true;
        }
        if (prop == "boxtype") {
            member->setTextBoxType(textBoxTypeFromDatum(value));
            return true;
        }
        if (prop == "rect") {
            const auto* rect = value.asIntRect();
            if (rect == nullptr) {
                return false;
            }
            member->setTextRect(rect->left, rect->top, rect->right, rect->bottom);
            return true;
        }
        if (prop == "width") {
            member->setTextWidth(value.intValue());
            return true;
        }
        if (prop == "height") {
            member->setTextHeight(value.intValue());
            return true;
        }
        if (prop == "fixedlinespace") {
            member->setTextFixedLineSpace(value.intValue());
            return true;
        }
        if (prop == "topspacing") {
            member->setTextTopSpacing(value.intValue());
            return true;
        }
        if (prop == "editable") {
            member->setEditable(value.intValue() != 0);
            return true;
        }
    }
    if (member->isBitmap() && prop == "alphathreshold") {
        member->setBitmapAlphaThreshold(value.intValue());
        return true;
    }
    if (member->isShape()) {
        if (prop == "filled") {
            member->setShapeFilled(value.boolValue());
            return true;
        }
        if (prop == "shapetype") {
            const auto type = shapeTypeFromDatum(value);
            if (!type.has_value()) {
                return false;
            }
            member->setShapeType(*type);
            return true;
        }
        if (prop == "linesize") {
            member->setShapeLineSize(value.intValue());
            return true;
        }
        if (prop == "pattern") {
            member->setShapePattern(value.intValue());
            return true;
        }
        if (prop == "width") {
            member->setShapeWidth(value.intValue());
            return true;
        }
        if (prop == "height") {
            member->setShapeHeight(value.intValue());
            return true;
        }
        if (prop == "rect") {
            const auto* rect = value.asIntRect();
            if (rect == nullptr) {
                return false;
            }
            member->setShapeSize(rect->width(), rect->height());
            return true;
        }
    }
    if (member->isBitmap() && prop == "width") {
        const int new_width = value.intValue();
        if (new_width > 0) {
            auto current = member->runtimeBitmap();
            const int height = current ? current->height() : 1;
            const int depth = current ? current->bitDepth() : 32;
            bitmap::Bitmap resized(new_width, height, depth);
            resized.fill(0xFFFFFFFFU);
            resized.setAnchorPoint(member->regX(), member->regY());
            member->setRuntimeBitmap(resized);
        }
        return true;
    }
    if (member->isBitmap() && prop == "height") {
        const int new_height = value.intValue();
        if (new_height > 0) {
            auto current = member->runtimeBitmap();
            const int width = current ? current->width() : 1;
            const int depth = current ? current->bitDepth() : 32;
            bitmap::Bitmap resized(width, new_height, depth);
            resized.fill(0xFFFFFFFFU);
            resized.setAnchorPoint(member->regX(), member->regY());
            member->setRuntimeBitmap(resized);
        }
        return true;
    }
    if (prop == "image") {
        const auto* image = value.asImageRef();
        if (image == nullptr || image->bitmap == nullptr) {
            return false;
        }
        member->setRuntimeBitmap(*image->bitmap);
        return true;
    }
    if (prop == "media" && member->isBitmap()) {
        const auto* image = value.asImageRef();
        if (image == nullptr || image->bitmap == nullptr) {
            return false;
        }
        member->setRuntimeBitmap(*image->bitmap);
        return true;
    }
    return false;
}

bool CastLib::setExternalData(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return false;
    }

    try {
        fetched_external_data_ = data;
        auto file = DirectorFile::load(data);
        if (!file) {
            // The fetched bytes are not a Director file (e.g. an HTTP
            // error page). Settle the cast as Failed so it stops blocking
            // the external-cast gate instead of staying in Fetching.
            state_ = State::Failed;
            return false;
        }

        source_file_ = std::move(file);
        const auto name_before_load = name_;
        if (auto external_cast_list = source_file_->castList();
            external_cast_list && !external_cast_list->entries().empty()) {
            const auto& internal_name = external_cast_list->entries().front().name;
            if (!internal_name.empty() && !name_explicitly_assigned_) {
                name_ = internal_name;
                name_loaded_from_external_file_ = !usesAuthoredExternalBinding();
            }
        }
        if (name_.empty()) {
            name_ = name_before_load;
            name_loaded_from_external_file_ = false;
        }

        state_ = State::None;
        member_chunks_.clear();
        members_.clear();
        invalidateMemberNameIndex();
        scripts_.clear();
        script_types_by_pointer_.clear();
        script_types_by_id_.clear();
        cached_scripts_.clear();
        total_slot_count_ = 0;
        load();
        return true;
    } catch (const std::exception&) {
        state_ = State::Failed;
        return false;
    }
}

void CastLib::cacheFetchedExternalData(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return;
    }
    fetched_external_data_ = data;
    const auto data_string = std::string(data.begin(), data.end());
    if (data_string.find("font") != std::string::npos || data_string.find("FONT") != std::string::npos) {
        try {
            registerFontAliases(DirectorFile::load(data));
        } catch (const std::exception&) {
        }
    }
}

lingo::Datum CastLib::getInvalidMemberProp(const std::string& prop_name) {
    const auto prop = lower(prop_name);
    if (prop == "name") return stringDatum("");
    if (prop == "number" || prop == "membernum") return lingo::Datum::of(0);
    if (prop == "type") return lingo::Datum::symbol("empty");
    return lingo::Datum::voidValue();
}

void CastLib::registerFontAliases(const std::shared_ptr<DirectorFile>& file) {
    if (!file) {
        return;
    }
    for (const auto& member : file->castMembers()) {
        if (!member) {
            continue;
        }
        if (const auto alias_info = parseFontAlias(member->specificData(), member->name()); alias_info.has_value()) {
            FontRegistry::registerFontAlias(alias_info->alias, alias_info->font_name, alias_info->bold);
        }
    }
}

int CastLib::minMember() const {
    if (!source_file_) {
        return 1;
    }
    if (auto cast_list = source_file_->castList();
        cast_list && cast_lib_id_.value() - 1 < static_cast<int>(cast_list->entries().size())) {
        const int value = cast_list->entries()[static_cast<std::size_t>(cast_lib_id_.value() - 1)].min_member;
        return value > 0 ? value : 1;
    }
    if (auto config = source_file_->config()) {
        const int value = config->minMember();
        return value > 0 ? value : 1;
    }
    return 1;
}

void CastLib::cacheScriptTypeForMember(int member_number, const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return;
    }
    auto script_type = script->scriptType();
    if (const auto found = member_chunks_.find(member_number);
        found != member_chunks_.end() && found->second) {
        if (const auto member_type = found->second->getScriptType(); member_type.has_value()) {
            script_type = castMemberScriptTypeToScriptChunkType(member_type.value());
        }
    }

    script_types_by_pointer_[script.get()] = script_type;
    script_types_by_id_[script->id().value()] = script_type;
}

void CastLib::loadMembersFromCast(const std::shared_ptr<chunks::CastChunk>& cast, int min_member_value) {
    if (!cast || !source_file_) {
        return;
    }

    total_slot_count_ = static_cast<int>(cast->memberIds().size()) + min_member_value - 1;
    for (int index = 0; index < static_cast<int>(cast->memberIds().size()); ++index) {
        const int chunk_id = cast->memberIds()[static_cast<std::size_t>(index)];
        if (chunk_id <= 0) {
            continue;
        }

        const int member_number = index + min_member_value;
        for (const auto& member : source_file_->castMembers()) {
            if (!member || member->id().value() != chunk_id) {
                continue;
            }
            member_chunks_[member_number] = member;
            if (member->isScript() && member->scriptId() > 0) {
                if (auto script = source_file_->getScriptForCastMember(member, cast)) {
                    scripts_[member_number] = script;
                    cacheScriptTypeForMember(member_number, script);
                }
            }
            break;
        }
    }
}

void CastLib::loadFromExternalFile() {
    if (!source_file_ || source_file_->casts().empty()) {
        return;
    }

    int min_member_value = 1;
    if (auto external_cast_list = source_file_->castList();
        external_cast_list && !external_cast_list->entries().empty()) {
        min_member_value = external_cast_list->entries().front().min_member;
    } else if (auto config = source_file_->config()) {
        min_member_value = config->minMember();
    }
    if (min_member_value <= 0) {
        min_member_value = 1;
    }

    loadMembersFromCast(source_file_->casts().front(), min_member_value);
}

void CastLib::scanXmedFonts() {
    if (!source_file_) {
        return;
    }

    registerFontAliases(source_file_);
    const auto key_table = source_file_->keyTable();
    if (!key_table) {
        return;
    }

    const auto xmed_four_cc = io::BinaryReader::fourCC("XMED");
    for (const auto& member : source_file_->castMembers()) {
        if (!member) {
            continue;
        }
        for (const auto& chunk : source_file_->getLinkedChunksForMember(member, xmed_four_cc)) {
            auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk);
            if (!raw || raw->data().size() < 4) {
                continue;
            }
            const auto& data = raw->data();
            if (data[0] == static_cast<std::uint8_t>('P') &&
                data[1] == static_cast<std::uint8_t>('F') &&
                data[2] == static_cast<std::uint8_t>('R') &&
                data[3] == static_cast<std::uint8_t>('1') &&
                !member->name().empty()) {
                FontRegistry::registerPfr1Font(member->name(), data);
            }
        }
    }
}

void CastLib::invalidateFileBackedBinding() {
    source_file_.reset();
    fetched_external_data_.clear();
    state_ = State::None;
    total_slot_count_ = 0;
    name_loaded_from_external_file_ = false;
    member_chunks_.clear();
    members_.clear();
    invalidateMemberNameIndex();
    scripts_.clear();
    script_types_by_pointer_.clear();
    script_types_by_id_.clear();
    cached_scripts_.clear();
}

bool CastLib::sameFileBinding(const std::string& current_file_name, const std::string& new_file_name) const {
    return current_file_name == new_file_name || (current_file_name.empty() && new_file_name.empty());
}

bool CastLib::usesGeneratedPlaceholderName(const std::string& candidate_name) const {
    if (candidate_name.empty()) {
        return true;
    }
    const auto authored_base_name = util::getFileNameWithoutExtension(util::getFileName(authored_file_name_));
    if (authored_base_name.empty()) {
        return false;
    }

    const auto normalized_name = lower(trim(candidate_name));
    const auto normalized_base = lower(trim(authored_base_name));
    if (normalized_name == normalized_base) {
        return true;
    }
    const auto prefix = normalized_base + " ";
    if (!startsWithIgnoreCase(normalized_name, prefix)) {
        return false;
    }
    const auto suffix = normalized_name.substr(prefix.size());
    return !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool CastLib::looksLikeDirectFileBindingName(const std::string& candidate_name) const {
    const auto normalized_name = lower(trim(candidate_name));
    if (normalized_name.empty()) {
        return true;
    }
    if (normalized_name.find("://") != std::string::npos ||
        normalized_name.find('/') != std::string::npos ||
        normalized_name.find('\\') != std::string::npos ||
        normalized_name.find('?') != std::string::npos ||
        normalized_name.find('#') != std::string::npos) {
        return true;
    }
    if (endsWith(normalized_name, ".cct") ||
        endsWith(normalized_name, ".cst") ||
        endsWith(normalized_name, ".dcr") ||
        endsWith(normalized_name, ".dir")) {
        return true;
    }
    return !file_name_.empty() && normalized_name == lower(trim(file_name_));
}

std::string CastLib::resolveMemberText(const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (!member || !member->isTextLike()) {
        return "";
    }
    if (member->hasDynamicText()) {
        return member->textContent();
    }
    if (source_file_ && member->rawChunk()) {
        if (auto styled_text = source_file_->getXmedStyledTextForMember(member->rawChunk())) {
            return normalizeTextLineEndings(styled_text->text);
        }
        if (auto text = source_file_->getTextForMember(member->rawChunk())) {
            return normalizeTextLineEndings(text->text());
        }
    }
    return "";
}

int CastLib::nextAvailableDynamicMemberNumber() {
    while (member_chunks_.contains(next_dynamic_member_) || members_.contains(next_dynamic_member_)) {
        ++next_dynamic_member_;
    }
    return next_dynamic_member_++;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberChunkByNameExact(const std::string& member_name) {
    if (member_name.empty()) {
        return nullptr;
    }
    rebuildMemberNameIndex();
    const auto found_index = member_chunk_name_index_.find(lower(member_name));
    if (found_index == member_chunk_name_index_.end()) {
        return nullptr;
    }
    const auto found_member = member_chunks_.find(found_index->second);
    return found_member == member_chunks_.end() ? nullptr : found_member->second;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::findMemberByNameExact(const std::string& member_name) {
    if (member_name.empty()) {
        return nullptr;
    }
    rebuildMemberNameIndex();
    const auto key = lower(member_name);
    const auto found_chunk_index = member_chunk_name_index_.find(key);
    if (found_chunk_index != member_chunk_name_index_.end()) {
        return getMember(found_chunk_index->second);
    }
    const auto found_runtime_index = runtime_member_name_index_.find(key);
    if (found_runtime_index == runtime_member_name_index_.end()) {
        return nullptr;
    }
    const auto found_member = members_.find(found_runtime_index->second);
    return found_member == members_.end() ? nullptr : found_member->second;
}

void CastLib::invalidateMemberNameIndex() const {
    member_name_index_dirty_ = true;
}

void CastLib::rebuildMemberNameIndex() const {
    if (!member_name_index_dirty_) {
        return;
    }

    member_chunk_name_index_.clear();
    runtime_member_name_index_.clear();
    member_chunk_name_index_.reserve(member_chunks_.size());
    runtime_member_name_index_.reserve(members_.size());
    for (const auto& [number, member] : member_chunks_) {
        if (member && !member->name().empty()) {
            member_chunk_name_index_[lower(member->name())] = number;
        }
    }
    for (const auto& [number, member] : members_) {
        if (member && !member->name().empty()) {
            runtime_member_name_index_[lower(member->name())] = number;
        }
    }
    member_name_index_dirty_ = false;
}

std::optional<std::string> CastLib::sourcePrefixedLookupName(const std::string& requested_name) {
    if (requested_name.empty() || startsWithIgnoreCase(requested_name, "s_")) {
        return std::nullopt;
    }
    return "s_" + requested_name;
}

libreshockwave::cast::MemberType CastLib::dynamicMemberTypeFor(const std::string& type_name) {
    const auto type = lower(type_name);
    if (type == "field" || type == "text") return libreshockwave::cast::MemberType::Text;
    if (type == "bitmap") return libreshockwave::cast::MemberType::Bitmap;
    if (type == "palette") return libreshockwave::cast::MemberType::Palette;
    if (type == "script") return libreshockwave::cast::MemberType::Script;
    if (type == "button") return libreshockwave::cast::MemberType::Button;
    if (type == "shape") return libreshockwave::cast::MemberType::Shape;
    if (type == "sound") return libreshockwave::cast::MemberType::Sound;
    return libreshockwave::cast::MemberType::Text;
}

bool CastLib::sameAuthoredMember(const std::shared_ptr<chunks::CastMemberChunk>& left,
                                 const std::shared_ptr<chunks::CastMemberChunk>& right) {
    if (left == right) {
        return true;
    }
    return left && right && left->file() == right->file() && left->id().value() == right->id().value();
}

std::optional<CastLib::FontAliasInfo> CastLib::parseFontAlias(const std::vector<std::uint8_t>& data,
                                                              const std::string& member_name) {
    if (data.size() < 12) {
        return std::nullopt;
    }

    const auto strings = extractPrintableNullTerminatedStrings(data);
    if (strings.size() < 2 || !equalsIgnoreCase(strings.front(), "font")) {
        return std::nullopt;
    }

    std::string font_name;
    for (auto it = strings.rbegin(); it != strings.rend(); ++it) {
        if (!equalsIgnoreCase(*it, "font")) {
            font_name = *it;
            break;
        }
    }
    if (trim(font_name).empty()) {
        return std::nullopt;
    }

    std::string alias = member_name;
    if (trim(alias).empty() && strings.size() >= 3) {
        alias = strings[strings.size() - 2];
    }
    if (trim(alias).empty() || equalsIgnoreCase(alias, "fontName")) {
        return std::nullopt;
    }

    const bool bold = data.size() > 23 && (data[23] & 0x01U) != 0U;
    return FontAliasInfo{alias, font_name, bold};
}

std::vector<std::string> CastLib::extractPrintableNullTerminatedStrings(const std::vector<std::uint8_t>& data) {
    std::vector<std::string> strings;
    int start = -1;
    for (int index = 0; index <= static_cast<int>(data.size()); ++index) {
        const int value = index < static_cast<int>(data.size()) ? static_cast<int>(data[static_cast<std::size_t>(index)]) : 0;
        const bool printable = value >= 0x20 && value <= 0x7E;
        if (printable) {
            if (start < 0) {
                start = index;
            }
        } else if (start >= 0) {
            if (value == 0 && index > start) {
                strings.emplace_back(data.begin() + start, data.begin() + index);
            }
            start = -1;
        }
    }
    return strings;
}

} // namespace libreshockwave::player::cast
