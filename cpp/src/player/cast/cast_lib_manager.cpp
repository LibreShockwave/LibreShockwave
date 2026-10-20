#include "libreshockwave/player/cast/cast_lib_manager.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/bitmap_decoder.hpp"
#include "libreshockwave/bitmap/gif_decoder.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/bitmap/png_decoder.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/xmed_styled_text.hpp"
#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/lingo/lingo_value_parser.hpp"
#include "libreshockwave/player/bitmap_resolver.hpp"
#include "libreshockwave/player/render/output/text_renderer.hpp"
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

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    return palette != nullptr
        ? std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {})
        : nullptr;
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lower(lhs) == lower(rhs);
}

std::optional<std::string_view> temporaryMemberTypeForExchange(const ::libreshockwave::cast::CastMember& first,
                                                               const ::libreshockwave::cast::CastMember& second) {
    if (first.isBitmap() && second.isBitmap()) {
        return "bitmap";
    }
    if (first.isTextLike() && second.isTextLike()) {
        return "text";
    }
    if (first.isPalette() && second.isPalette()) {
        return "palette";
    }
    if (first.isScript() && second.isScript()) {
        return "script";
    }
    return std::nullopt;
}

std::optional<std::string> normalizeUnderscoreNumericTokens(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool changed = false;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto next = value.find('_', offset);
        const auto token = value.substr(offset, next == std::string_view::npos ? std::string_view::npos : next - offset);
        if (offset > 0) {
            normalized.push_back('_');
        }

        const bool numeric = !token.empty() &&
                             std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                                 return std::isdigit(ch) != 0;
                             });
        if (numeric && token.size() > 1) {
            std::size_t first_non_zero = 0;
            while (first_non_zero + 1 < token.size() && token[first_non_zero] == '0') {
                ++first_non_zero;
            }
            if (first_non_zero > 0) {
                changed = true;
            }
            normalized.append(token.substr(first_non_zero));
        } else {
            normalized.append(token);
        }

        if (next == std::string_view::npos) {
            break;
        }
        offset = next + 1;
    }
    if (!changed) {
        return std::nullopt;
    }
    return normalized;
}

std::string keyNameLikeJava(const lingo::Datum& value) {
    if (const auto* symbol = value.asSymbol()) {
        return symbol->name;
    }
    return value.stringValue();
}

struct ResolvedPaletteOverride {
    std::shared_ptr<const bitmap::Palette> palette;
    std::optional<lingo::Datum::CastMemberRef> member_ref;
    std::optional<std::string> system_name;
};

std::optional<ResolvedPaletteOverride> resolvePaletteMemberOverrideByName(CastLibManager& manager,
                                                                           const std::string& name) {
    for (const auto& [_, cast_lib] : manager.castLibs()) {
        if (!cast_lib) {
            continue;
        }
        auto member = cast_lib->getPaletteMemberByName(name);
        if (!member) {
            continue;
        }
        if (auto palette = manager.resolvePaletteByMember(cast_lib->number(), member->memberNum())) {
            return ResolvedPaletteOverride{
                palette,
                lingo::Datum::CastMemberRef::of(id::CastLibId(cast_lib->number()), id::MemberId(member->memberNum())),
                std::nullopt
            };
        }
    }
    return std::nullopt;
}

std::optional<ResolvedPaletteOverride> resolvePaletteOverrideDatum(CastLibManager& manager,
                                                                   const lingo::Datum& value,
                                                                   int default_cast_lib) {
    if (const auto* ref = value.asCastMemberRef()) {
        const int cast_lib = ref->cast_lib > 0 ? ref->cast_lib : default_cast_lib;
        if (auto palette = manager.resolvePaletteByMember(cast_lib, ref->memberNum())) {
            return ResolvedPaletteOverride{
                palette,
                lingo::Datum::CastMemberRef::of(id::CastLibId(cast_lib), id::MemberId(ref->memberNum())),
                std::nullopt
            };
        }
    }

    if (value.isString() || value.isSymbol()) {
        const std::string name = keyNameLikeJava(value);
        if (const auto* built_in = bitmap::Palette::builtInBySymbolName(name)) {
            const auto normalized = bitmap::Palette::normalizeBuiltInSymbolName(name);
            return ResolvedPaletteOverride{borrowedPalette(built_in), std::nullopt, normalized};
        }

        if (auto named_palette = resolvePaletteMemberOverrideByName(manager, name)) {
            return named_palette;
        }

        if (auto palette = manager.resolvePaletteByName(name)) {
            return ResolvedPaletteOverride{palette, std::nullopt, std::nullopt};
        }
    }

    return std::nullopt;
}

int countMethodWords(std::string_view text) {
    int count = 0;
    bool in_word = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            in_word = false;
        } else if (!in_word) {
            ++count;
            in_word = true;
        }
    }
    return count;
}

int countMethodTextChunks(std::string_view text, const std::string& chunk_type) {
    if (text.empty()) {
        return 0;
    }
    const std::string type = lower(chunk_type);
    if (type == "char") {
        return static_cast<int>(text.size());
    }
    if (type == "word") {
        return countMethodWords(text);
    }
    if (type == "line") {
        return static_cast<int>(std::count(text.begin(), text.end(), '\r') +
                                std::count(text.begin(), text.end(), '\n') + 1);
    }
    if (type == "item") {
        return static_cast<int>(std::count(text.begin(), text.end(), ',') + 1);
    }
    return 0;
}

int measureAutoTextWidth(const std::shared_ptr<libreshockwave::cast::CastMember>& member,
                         render::output::TextRenderer* renderer,
                         const std::string& text,
                         int rect_width) {
    int max_width = std::max(1, rect_width);
    for (const auto& line : render::output::TextRenderer::splitLines(text)) {
        auto loc = renderer->charPosToLoc(line,
                                          static_cast<int>(line.size()) + 1,
                                          member->textFont(),
                                          member->textFontSize(),
                                          member->textFontStyle(),
                                          member->textFixedLineSpace(),
                                          "left",
                                          0);
        if (!loc.empty()) {
            max_width = std::max(max_width, loc[0] + 2);
        }
    }
    return max_width;
}

std::shared_ptr<bitmap::Bitmap> renderTextMemberImage(
    CastLib& cast_lib,
    const std::shared_ptr<libreshockwave::cast::CastMember>& member,
    render::output::TextRenderer* renderer) {
    if (!member || !member->isTextLike() || renderer == nullptr) {
        return nullptr;
    }

    const std::string text = cast_lib.getMemberProp(member->memberNum(), "text").stringValue();
    const int rect_width = member->textRectRight() - member->textRectLeft();
    int width = rect_width;
    if (member->textBoxType() == 0 && !member->textWordWrap()) {
        width = std::max(width, measureAutoTextWidth(member, renderer, text, rect_width));
    }
    const int height = member->textBoxType() == 0 ? 0 : member->textRectBottom() - member->textRectTop();
    auto rendered = renderer->renderTextInField(text,
                                         width,
                                         height,
                                         member->textFont(),
                                         member->textFontSize(),
                                         member->textFontStyle(),
                                         member->textAlignment(),
                                         member->textColor(),
                                         member->textBgColor(),
                                         member->textWordWrap(),
                                         member->textAntialias(),
                                         member->textFixedLineSpace(),
                                         member->textTopSpacing(),
                                         member->textGutterSize());
    if (rendered != nullptr &&
        ((((static_cast<std::uint32_t>(member->textBgColor()) >> 24U) & 0xFFU) < 0xFFU) ||
         rendered->hasTransparentPixels())) {
        rendered->setNativeAlpha(true);
    }
    return rendered;
}

std::uint32_t readU32BE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

std::uint32_t readU32LE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

std::optional<std::size_t> findBitdMarker(const std::vector<std::uint8_t>& data) {
    if (data.size() < 40) {
        return std::nullopt;
    }
    for (std::size_t index = 32; index + 8 <= data.size(); ++index) {
        if (data[index] == static_cast<std::uint8_t>('D') &&
            data[index + 1] == static_cast<std::uint8_t>('T') &&
            data[index + 2] == static_cast<std::uint8_t>('I') &&
            data[index + 3] == static_cast<std::uint8_t>('B')) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<bitmap::Bitmap> decodeImportedImage(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12 ||
        data[0] != static_cast<std::uint8_t>('L') ||
        data[1] != static_cast<std::uint8_t>('S') ||
        data[2] != static_cast<std::uint8_t>('W') ||
        data[3] != static_cast<std::uint8_t>('I')) {
        return std::nullopt;
    }

    const auto raw_width = readU32BE(data, 4);
    const auto raw_height = readU32BE(data, 8);
    if (raw_width == 0 ||
        raw_height == 0 ||
        raw_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        raw_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const std::size_t width = raw_width;
    const std::size_t height = raw_height;
    if (width > (std::numeric_limits<std::size_t>::max() / height) ||
        width * height > (std::numeric_limits<std::size_t>::max() / 4U)) {
        return std::nullopt;
    }

    const std::size_t pixel_bytes = width * height * 4U;
    if (data.size() < 12U + pixel_bytes) {
        return std::nullopt;
    }

    bitmap::Bitmap bitmap(static_cast<int>(width), static_cast<int>(height), 32);
    bool has_alpha = false;
    std::size_t offset = 12;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const int r = data[offset++];
            const int g = data[offset++];
            const int b = data[offset++];
            const int a = data[offset++];
            if (a < 255) {
                has_alpha = true;
            }
            bitmap.setPixel(x,
                            y,
                            (static_cast<std::uint32_t>(a & 0xFF) << 24U) |
                                (static_cast<std::uint32_t>(r & 0xFF) << 16U) |
                                (static_cast<std::uint32_t>(g & 0xFF) << 8U) |
                                static_cast<std::uint32_t>(b & 0xFF));
        }
    }
    if (has_alpha) {
        bitmap.setNativeAlpha(true);
    }
    return bitmap;
}

std::shared_ptr<const bitmap::Palette> resolveMediaPalette(const ::libreshockwave::cast::BitmapInfo& info,
                                                           int default_cast_lib,
                                                           CastLibManager* manager) {
    if (info.palette_id < 0) {
        return std::shared_ptr<const bitmap::Palette>(
            &bitmap::Palette::builtIn(info.palette_id),
            [](const bitmap::Palette*) {});
    }

    const int member_number = info.palette_id + 1;
    if (manager != nullptr) {
        if (info.palette_cast_lib > 0) {
            if (auto palette = manager->resolvePaletteById(info.palette_cast_lib, info.palette_id)) {
                return palette;
            }
            if (auto palette = manager->resolvePaletteByMember(info.palette_cast_lib, member_number)) {
                return palette;
            }
        }
        if (default_cast_lib > 0) {
            if (auto palette = manager->resolvePaletteById(default_cast_lib, info.palette_id)) {
                return palette;
            }
            if (auto palette = manager->resolvePaletteByMember(default_cast_lib, member_number)) {
                return palette;
            }
        }
    }
    return std::shared_ptr<const bitmap::Palette>(
        &bitmap::Palette::systemMacPalette(),
        [](const bitmap::Palette*) {});
}

std::optional<bitmap::Bitmap> decodeDirectorBitmapMedia(const std::vector<std::uint8_t>& data,
                                                        int default_cast_lib,
                                                        CastLibManager* manager) {
    const auto marker = findBitdMarker(data);
    if (!marker.has_value() || *marker < 32 || *marker + 8 > data.size()) {
        return std::nullopt;
    }

    const auto bitd_len = readU32LE(data, *marker + 4);
    const std::size_t bitd_start = *marker + 8;
    if (bitd_len == 0 || bitd_len > data.size() - bitd_start) {
        return std::nullopt;
    }

    const std::vector<std::uint8_t> info_data(data.begin() + static_cast<std::ptrdiff_t>(*marker - 32),
                                             data.begin() + static_cast<std::ptrdiff_t>(*marker));
    const auto info = ::libreshockwave::cast::BitmapInfo::parse(info_data, 1200);
    if (info.width <= 0 || info.height <= 0) {
        return std::nullopt;
    }

    const std::vector<std::uint8_t> bitd(data.begin() + static_cast<std::ptrdiff_t>(bitd_start),
                                         data.begin() + static_cast<std::ptrdiff_t>(bitd_start + bitd_len));
    std::shared_ptr<const bitmap::Palette> palette;
    if (info.bit_depth <= 8) {
        palette = resolveMediaPalette(info, default_cast_lib, manager);
    }

    auto bitmap = bitmap::BitmapDecoder::decode(bitd,
                                                info.width,
                                                info.height,
                                                info.bit_depth,
                                                palette.get(),
                                                true,
                                                1200,
                                                info.pitch);
    if (palette) {
        bitmap.setImagePalette(palette);
    }
    bitmap.setAnchorPoint(info.regXLocal(), info.regYLocal());
    return bitmap;
}

std::optional<bitmap::Bitmap> decodeBitmapMedia(const std::vector<std::uint8_t>& data,
                                                int default_cast_lib,
                                                CastLibManager* manager) {
    auto bitmap = decodeImportedImage(data);
    if (!bitmap.has_value()) {
        bitmap = bitmap::PngDecoder::decode(data);
        if (bitmap.has_value()) {
            bitmap = bitmap->copyWithDegenerateNativeAlphaOpaque();
        }
    }
    if (!bitmap.has_value()) {
        bitmap = bitmap::GifDecoder::decode(data);
    }
    if (!bitmap.has_value()) {
        bitmap = decodeDirectorBitmapMedia(data, default_cast_lib, manager);
        if (bitmap.has_value()) {
            bitmap = bitmap->copyWithDegenerateNativeAlphaOpaque();
        }
    }
    return bitmap;
}

std::shared_ptr<const bitmap::Palette> clonePalette(const bitmap::Palette& palette) {
    return std::make_shared<bitmap::Palette>(palette.colors(), palette.name());
}

std::optional<::libreshockwave::cast::XmedStyledText> authoredXmedText(
    const std::shared_ptr<CastLib>& cast_lib,
    const std::shared_ptr<::libreshockwave::cast::CastMember>& member) {
    if (!cast_lib || !member || !member->rawChunk()) {
        return std::nullopt;
    }
    const auto source_file = cast_lib->sourceFile();
    if (!source_file) {
        return std::nullopt;
    }
    return source_file->getXmedStyledTextForMember(member->rawChunk());
}

void applyTextStyleFromXmed(::libreshockwave::cast::CastMember& target,
                            const ::libreshockwave::cast::XmedStyledText& styled) {
    if (!styled.font_name.empty()) {
        target.setTextFont(styled.font_name);
    }
    if (styled.font_size > 0) {
        target.setTextFontSize(styled.font_size);
    }
    target.setTextFontStyle(styled.fontStyleString().empty() ? "plain" : styled.fontStyleString());
    if (!styled.alignment.empty()) {
        target.setTextAlignment(styled.alignment);
    }
    target.setTextColor(static_cast<int>(styled.textColorARGB()));
    target.setTextWordWrap(styled.word_wrap);
    target.setTextAntialias(styled.antialias);
    if (styled.width > 0 && styled.height > 0) {
        target.setTextRect(0, 0, styled.width, styled.height);
    }
    target.setTextFixedLineSpace(styled.fixed_line_space);
}

std::shared_ptr<const bitmap::Palette> paletteFromCastMember(const std::shared_ptr<CastLib>& cast_lib,
                                                             int member_number) {
    if (!cast_lib) {
        return nullptr;
    }
    if (!cast_lib->isLoaded()) {
        cast_lib->load();
    }
    auto member = cast_lib->getMember(member_number);
    if (member && member->isPalette()) {
        if (auto palette = member->paletteData()) {
            return palette;
        }
    }
    if (cast_lib->sourceFile() != nullptr) {
        return cast_lib->sourceFile()->resolvePaletteByMemberNumber(member_number);
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> paletteFromCastById(const std::shared_ptr<CastLib>& cast_lib,
                                                           int palette_id) {
    if (!cast_lib) {
        return nullptr;
    }

    auto source = cast_lib->sourceFile();
    if (source) {
        if (auto palette = source->resolvePaletteExact(palette_id)) {
            return palette;
        }
    }

    if (!cast_lib->isLoaded()) {
        cast_lib->load();
    }

    const int member_number = palette_id + 1;
    auto member = cast_lib->getMember(member_number);
    if (member && member->isPalette()) {
        if (auto palette = member->paletteData()) {
            return palette;
        }
    }

    auto chunk = cast_lib->findMemberByNumber(member_number);
    if (chunk && source) {
        return source->resolvePaletteByMemberNumber(member_number);
    }
    return nullptr;
}

std::optional<lingo::Datum::CastMemberRef> duplicateTargetRefFromArg(const lingo::Datum& target_arg,
                                                                     int default_cast_lib) {
    if (const auto* target_ref = target_arg.asCastMemberRef()) {
        return *target_ref;
    }
    if (!target_arg.isNumber()) {
        return std::nullopt;
    }

    const int slot_value = target_arg.intValue();
    const id::SlotId slot_id(slot_value);
    if (slot_id.castLib() >= 1 && slot_id.member() >= 1) {
        return lingo::Datum::CastMemberRef::of(id::CastLibId(slot_id.castLib()),
                                               id::MemberId(slot_id.member()));
    }
    if (default_cast_lib >= 1 && slot_value >= 1) {
        return lingo::Datum::CastMemberRef::of(id::CastLibId(default_cast_lib), id::MemberId(slot_value));
    }
    return std::nullopt;
}

} // namespace

CastLibManager::CastLibManager(std::shared_ptr<DirectorFile> file,
                               CastDataRequestCallback cast_data_request_callback)
    : file_(std::move(file)),
      cast_data_request_callback_(std::move(cast_data_request_callback)) {}

std::shared_ptr<DirectorFile> CastLibManager::file() const {
    return file_;
}

std::shared_ptr<CastLib> CastLibManager::getCastLib(int cast_lib_number) {
    ensureInitialized();
    if (const auto found = cast_libs_.find(cast_lib_number); found != cast_libs_.end()) {
        if (found->second && !found->second->isLoaded()) {
            found->second->load();
        }
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<CastLib> CastLibManager::getCastLibByNameInternal(const std::string& name) {
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (cast_lib && equalsIgnoreCase(cast_lib->name(), name)) {
            if (!cast_lib->isLoaded()) {
                cast_lib->load();
            }
            return cast_lib;
        }
    }
    if (equalsIgnoreCase(name, "internal")) {
        return getCastLib(1);
    }
    return nullptr;
}

int CastLibManager::getCastLibByNumber(int cast_lib_number) {
    return getCastLib(cast_lib_number) ? cast_lib_number : -1;
}

int CastLibManager::getCastLibByName(const std::string& name) {
    auto cast_lib = getCastLibByNameInternal(name);
    return cast_lib ? cast_lib->number() : -1;
}

int CastLibManager::getCastLibCount() {
    ensureInitialized();
    return static_cast<int>(cast_libs_.size());
}

std::map<int, std::shared_ptr<CastLib>>& CastLibManager::castLibs() {
    ensureInitialized();
    return cast_libs_;
}

const std::map<int, std::shared_ptr<CastLib>>& CastLibManager::castLibs() const {
    return cast_libs_;
}

lingo::Datum CastLibManager::getCastLibProp(int cast_lib_number, const std::string& prop_name) {
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib ? cast_lib->getProp(prop_name) : lingo::Datum::voidValue();
}

bool CastLibManager::setCastLibProp(int cast_lib_number, const std::string& prop_name, const lingo::Datum& value) {
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib) {
        return false;
    }
    const bool result = cast_lib->setProp(prop_name, value);
    if (result && equalsIgnoreCase(prop_name, "filename")) {
        tryLoadCastFromCache(cast_lib_number, value.stringValue());
    }
    return result;
}

std::string CastLibManager::getCastLibName(int cast_lib_number) {
    ensureInitialized();
    if (const auto found = cast_libs_.find(cast_lib_number); found != cast_libs_.end() && found->second) {
        return found->second->name();
    }
    return "";
}

std::string CastLibManager::getCastLibFileName(int cast_lib_number) {
    ensureInitialized();
    if (const auto found = cast_libs_.find(cast_lib_number); found != cast_libs_.end() && found->second) {
        return found->second->fileName();
    }
    return "";
}

bool CastLibManager::fetchCastLib(int cast_lib_number) {
    ensureInitialized();
    if (const auto found = cast_libs_.find(cast_lib_number); found != cast_libs_.end() && found->second) {
        return found->second->isFetched();
    }
    return false;
}

bool CastLibManager::isCastLibExternal(int cast_lib_number) {
    ensureInitialized();
    if (const auto found = cast_libs_.find(cast_lib_number); found != cast_libs_.end() && found->second) {
        return found->second->isExternal();
    }
    return false;
}

void CastLibManager::preloadCasts(int mode) {
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (cast_lib && cast_lib->preloadMode() == mode && cast_lib->isFetched() && !cast_lib->isLoaded()) {
            cast_lib->load();
        }
    }
}

lingo::Datum CastLibManager::getMember(int cast_lib_number, int member_number) {
    if (cast_lib_number < 1 || member_number < 0) {
        return lingo::Datum::voidValue();
    }
    (void)getCastLib(cast_lib_number);
    return lingo::Datum::castMemberRef(id::CastLibId(cast_lib_number), id::MemberId(member_number));
}

lingo::Datum CastLibManager::getMemberByName(int cast_lib_number, const std::string& member_name) {
    ensureInitialized();
    if (cast_lib_number > 0) {
        return getMemberByNameInCast(getCastLib(cast_lib_number), member_name);
    }

    for (const auto& [_, cast_lib] : cast_libs_) {
        if (!isRegistryFallbackEligibleCast(cast_lib)) {
            continue;
        }
        auto loaded_cast = getCastLib(cast_lib->number());
        auto found = getMemberByNameInCast(loaded_cast, member_name);
        if (!found.isVoid()) {
            return found;
        }
    }

    for (const auto& [_, cast_lib] : cast_libs_) {
        auto found = getMemberByNameInCast(getCastLib(cast_lib->number()), member_name);
        if (!found.isVoid()) {
            return found;
        }
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::memberExists(int cast_lib_number, int member_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib && cast_lib->hasMemberNumber(member_number);
}

bool CastLibManager::isRegistryVisibleMember(int cast_lib_number, int member_number) {
    if (member_number <= 0) {
        return false;
    }
    auto cast_lib = getCastLib(cast_lib_number);
    return isRegistryFallbackEligibleCast(cast_lib) && cast_lib->getMember(member_number) != nullptr;
}

int CastLibManager::getMemberCount(int cast_lib_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib ? cast_lib->memberCount() : 0;
}

lingo::Datum CastLibManager::getMemberProp(int cast_lib_number, int member_number, const std::string& prop_name) {
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib) {
        return CastLib::getInvalidMemberProp(prop_name);
    }

    const auto prop = lower(prop_name);
    if (auto member = cast_lib->getMember(member_number)) {
        const auto info = member->bitmapInfo();
        const auto source_file = cast_lib->sourceFile();
        const bool uses_unlinked_palette = info.has_value() && info->palette_id >= 0 && source_file != nullptr &&
            source_file->resolvePaletteExact(info->palette_id) == nullptr;
        const bool host_uses_windows_palette = file_ != nullptr && file_->config() != nullptr &&
            file_->config()->platform() == 2 && uses_unlinked_palette;
        const bool runtime_needs_host_palette = host_uses_windows_palette && member->runtimeBitmap() != nullptr &&
            isSystemMacPalette(member->runtimeBitmap()->imagePalette()) &&
            member->runtimePaletteOverride() == nullptr;
        if (member->isBitmap() && prop == "image" && member->rawChunk() &&
            (!member->runtimeBitmap() || member->hasRuntimeBitmapDecodePlaceholder() ||
             member->shouldRedecodeAuthoredRuntimeBitmap() ||
             (member->runtimeBitmap() && !member->runtimeBitmap()->isScriptModified()) ||
             runtime_needs_host_palette)) {
            // Bitmap member properties are normally decoded by CastLib, which
            // only knows the external cast's own platform.  Resolve through
            // the host-aware bitmap resolver so unlinked external bitmaps use
            // the movie platform palette instead of an unrelated Mac CLUT.
            BitmapResolver resolver(file_, this, nullptr);
            const bitmap::Palette* decode_palette = nullptr;
            const auto runtime_palette_override = member->runtimePaletteOverride();
            if (file_ != nullptr && file_->config() != nullptr && file_->config()->platform() == 2 &&
                (uses_unlinked_palette || isSystemMacPalette(runtime_palette_override))) {
                decode_palette = &bitmap::Palette::systemWinPalette();
            }
            if (auto decoded = resolver.decodeBitmap(member->rawChunk(), decode_palette)) {
                member->setRuntimeBitmapFromAuthoredSource(*decoded);
            }
        }
        if (!member->isTextLike()) {
            if (prop == "width") {
                return lingo::Datum::of(member->width());
            }
            if (prop == "height") {
                return lingo::Datum::of(member->height());
            }
            if (prop == "rect") {
                return lingo::Datum::intRect(0, 0, member->width(), member->height());
            }
        } else {
            if (prop == "text") {
                const auto value = cast_lib->getMemberProp(member_number, prop_name);
                return lingo::Datum::fieldText(value.stringValue(), member->castLib(), member->memberNum(), member->textRevision() + 1);
            }
            if (prop == "image") {
                auto rendered = renderTextMemberImage(*cast_lib, member, text_renderer_);
                return rendered
                    ? lingo::Datum::imageRef(std::move(rendered))
                    : cast_lib->getMemberProp(member_number, prop_name);
            }
            if ((prop == "height" || prop == "rect") && member->textBoxType() == 0 && text_renderer_ != nullptr) {
                auto rendered = renderTextMemberImage(*cast_lib, member, text_renderer_);
                if (rendered) {
                    const int rect_height = member->textRectBottom() - member->textRectTop();
                    if (prop == "height") {
                        if (member->isRuntimeDynamic() && rect_height >= 256) {
                            return lingo::Datum::of(rendered->height());
                        }
                        return lingo::Datum::of(std::max(rendered->height(), rect_height));
                    }
                    if (rendered->height() > rect_height) {
                        return lingo::Datum::intRect(member->textRectLeft(),
                                                     member->textRectTop(),
                                                     member->textRectRight(),
                                                     member->textRectTop() + rendered->height());
                    }
                }
            }
        }
    }
    return cast_lib->getMemberProp(member_number, prop_name);
}

bool CastLibManager::setMemberProp(int cast_lib_number,
                                   int member_number,
                                   const std::string& prop_name,
                                   const lingo::Datum& value) {
    if (equalsIgnoreCase(prop_name, "paletteRef") || equalsIgnoreCase(prop_name, "palette")) {
        auto member = resolveMember(cast_lib_number, member_number);
        if (!member || !member->isBitmap()) {
            return false;
        }
        auto resolved = resolvePaletteOverrideDatum(*this, value, cast_lib_number);
        if (!resolved.has_value() || !resolved->palette) {
            return false;
        }

        int palette_ref_cast_lib = -1;
        int palette_ref_member_num = -1;
        if (resolved->member_ref.has_value()) {
            palette_ref_cast_lib = resolved->member_ref->cast_lib;
            palette_ref_member_num = resolved->member_ref->memberNum();
        }
        auto resolved_palette = resolved->palette;
        auto palette_ref_system_name = resolved->system_name;
        const auto info = member->bitmapInfo();
        const bool is_authored_indexed_bitmap_with_local_palette =
            member->rawChunk() != nullptr && info.has_value() && info->bit_depth <= 8 && info->palette_id >= 0;
        if (file_ != nullptr && file_->config() != nullptr && file_->config()->platform() == 2 &&
            is_authored_indexed_bitmap_with_local_palette && isSystemMacPalette(resolved_palette)) {
            resolved_palette = borrowedPalette(&bitmap::Palette::systemWinPalette());
            palette_ref_system_name = std::string("systemWin");
        }
        member->setRuntimePaletteOverride(resolved_palette,
                                          palette_ref_cast_lib,
                                          palette_ref_member_num,
                                          palette_ref_system_name,
                                          equalsIgnoreCase(prop_name, "palette"));
        return true;
    }
    if (equalsIgnoreCase(prop_name, "media")) {
        if (const auto* source_ref = value.asCastMemberRef()) {
            return copyMemberMedia(cast_lib_number, member_number, *source_ref);
        }
        if (const auto* media = value.asMedia()) {
            auto member = resolveMember(cast_lib_number, member_number);
            if (!member || !member->isBitmap()) {
                return false;
            }
            auto bitmap = decodeBitmapMedia(media->bytes, cast_lib_number, this);
            if (!bitmap.has_value()) {
                return false;
            }
            auto image = std::make_shared<bitmap::Bitmap>(std::move(*bitmap));
            auto cast_lib = getCastLib(cast_lib_number);
            return cast_lib && cast_lib->setMemberProp(member_number, "image", lingo::Datum::imageRef(std::move(image)));
        }
    }
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib && cast_lib->setMemberProp(member_number, prop_name, value);
}

lingo::Datum CastLibManager::getFieldValue(const lingo::Datum& identifier, int cast_lib_number) {
    auto member = resolveFieldMember(identifier, cast_lib_number);
    if (!member) {
        return lingo::Datum::of(std::string());
    }
    if (member->hasDynamicText()) {
        return lingo::Datum::of(member->textContent());
    }
    if (!member->isTextLike()) {
        return lingo::Datum::of(std::string());
    }
    auto text = getMemberProp(member->castLib(), member->memberNum(), "text");
    return text.isVoid() ? lingo::Datum::of(std::string()) : text;
}

lingo::Datum CastLibManager::getFieldDatum(const lingo::Datum& identifier, int cast_lib_number) {
    auto member = resolveFieldMember(identifier, cast_lib_number);
    if (!member) {
        return lingo::Datum::of(std::string());
    }
    lingo::Datum text = getFieldValue(identifier, cast_lib_number);
    return lingo::Datum::fieldText(text.stringValue(), member->castLib(), member->memberNum(), member->textRevision() + 1);
}

lingo::Datum CastLibManager::getParsedFieldValue(int cast_lib_number, int member_number, std::uint64_t revision) {
    const auto key = std::make_pair(cast_lib_number, member_number);
    if (revision != 0) {
        if (const auto found = parsed_field_cache_.find(key);
            found != parsed_field_cache_.end() && found->second.revision == revision) {
            return found->second.value;
        }
    }

    auto member = resolveMember(cast_lib_number, member_number);
    if (!member || !member->isTextLike()) {
        return lingo::Datum::voidValue();
    }

    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    if (const auto found = parsed_field_cache_.find(key);
        found != parsed_field_cache_.end() && found->second.text == text) {
        return found->second.value;
    }

    lingo::Datum parsed = lingo::LingoValueParser::parseWithPartial(text);
    parsed_field_cache_[key] = ParsedFieldCacheEntry{revision, text, parsed};
    return parsed;
}

std::vector<int> CastLibManager::charPosToLoc(int cast_lib_number, int member_number, int char_index, int field_width) {
    auto member = resolveMember(cast_lib_number, member_number);
    if (!member || !member->isTextLike() || !text_renderer_) {
        return {};
    }
    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    return text_renderer_->charPosToLoc(text,
                                       char_index,
                                       member->textFont(),
                                       member->textFontSize(),
                                       member->textFontStyle(),
                                       member->textFixedLineSpace(),
                                       member->textAlignment(),
                                       std::max(1, field_width));
}

int CastLibManager::locToCharPos(int cast_lib_number, int member_number, int x, int y, int field_width) {
    auto member = resolveMember(cast_lib_number, member_number);
    if (!member || !member->isTextLike() || !text_renderer_) {
        return 0;
    }
    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    if (text.empty()) {
        return 0;
    }
    return text_renderer_->locToCharPos(text,
                                       x,
                                       y,
                                       member->textFont(),
                                       member->textFontSize(),
                                       member->textFontStyle(),
                                       member->textFixedLineSpace(),
                                       member->textAlignment(),
                                       std::max(1, field_width));
}

int CastLibManager::textLineHeight(int cast_lib_number, int member_number) {
    auto member = resolveMember(cast_lib_number, member_number);
    if (!member || !member->isTextLike() || !text_renderer_) {
        return 0;
    }
    return text_renderer_->getLineHeight(member->textFont(),
                                        member->textFontSize(),
                                        member->textFontStyle(),
                                        member->textFixedLineSpace());
}

void CastLibManager::setFieldValue(const lingo::Datum& identifier, int cast_lib_number, const std::string& value) {
    auto member = resolveFieldMember(identifier, cast_lib_number);
    if (member) {
        member->setDynamicText(value);
    }
}

std::shared_ptr<chunks::CastMemberChunk> CastLibManager::getCastMember(int cast_lib_number, int member_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib ? cast_lib->findMemberByNumber(member_number) : nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastLibManager::getCastMemberByName(const std::string& member_name) {
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        auto member = getCastLib(cast_lib->number())->findMemberByName(member_name);
        if (member) {
            return member;
        }
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::resolveMember(int cast_lib_number, int member_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    return cast_lib ? cast_lib->getMember(member_number) : nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::findCastMemberByName(const std::string& member_name) {
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (cast_lib->isLoaded()) {
            if (auto dynamic = cast_lib->getMemberByName(member_name)) {
                return dynamic;
            }
        }
    }

    for (const auto& [_, cast_lib] : cast_libs_) {
        auto loaded_cast = getCastLib(cast_lib->number());
        if (auto chunk = loaded_cast->findMemberByName(member_name)) {
            const int member_number = loaded_cast->getMemberNumber(chunk);
            if (auto member = loaded_cast->getMember(member_number)) {
                return member;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::findRuntimeMember(
    const std::shared_ptr<chunks::CastMemberChunk>& target) {
    if (!target) {
        return nullptr;
    }
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (!cast_lib->isLoaded()) {
            cast_lib->load();
        }
        const int member_number = cast_lib->getMemberNumber(target);
        if (member_number >= 0) {
            return cast_lib->getMember(member_number);
        }
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> CastLibManager::resolvePaletteByMember(int cast_lib_number, int member_number) {
    if (member_number < 0) {
        return nullptr;
    }

    if (auto cast_lib = getCastLib(cast_lib_number)) {
        if (auto palette = paletteFromCastMember(cast_lib, member_number)) {
            return palette;
        }
    }

    ensureInitialized();
    for (const auto& [number, cast_lib] : cast_libs_) {
        if (number == cast_lib_number) {
            continue;
        }
        if (auto palette = paletteFromCastMember(cast_lib, member_number)) {
            return palette;
        }
    }

    return file_ != nullptr ? file_->resolvePaletteByMemberNumber(member_number) : nullptr;
}

std::shared_ptr<const bitmap::Palette> CastLibManager::resolvePaletteById(int cast_lib_number, int palette_id) {
    if (palette_id < 0) {
        return borrowedPalette(&bitmap::Palette::builtIn(palette_id));
    }

    ensureInitialized();
    if (cast_lib_number > 0) {
        if (auto palette = paletteFromCastById(getCastLib(cast_lib_number), palette_id)) {
            return palette;
        }
    }

    if (file_ != nullptr) {
        if (auto palette = file_->resolvePaletteExact(palette_id)) {
            return palette;
        }
    }

    for (const auto& [number, cast_lib] : cast_libs_) {
        if (cast_lib_number > 0 && number == cast_lib_number) {
            continue;
        }
        if (!cast_lib || !cast_lib->isLoaded()) {
            continue;
        }
        if (auto palette = paletteFromCastById(cast_lib, palette_id)) {
            return palette;
        }
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> CastLibManager::resolvePaletteByName(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    ensureInitialized();
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (!cast_lib) {
            continue;
        }
        if (!cast_lib->isLoaded()) {
            cast_lib->load();
        }
        auto member = cast_lib->getMemberByName(name);
        if (!member || !member->isPalette()) {
            continue;
        }
        if (auto palette = resolvePaletteByMember(cast_lib->number(), member->memberNum())) {
            return palette;
        }
    }
    return nullptr;
}

int CastLibManager::getScriptChunkId(int cast_lib_number, int member_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib || !cast_lib->isLoaded()) {
        return -1;
    }
    auto script = cast_lib->getScript(member_number);
    return script ? script->id().value() : -1;
}

std::vector<std::string> CastLibManager::getScriptPropertyNames(int cast_lib_number, int member_number) {
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib || !cast_lib->isLoaded()) {
        return {};
    }
    auto script = cast_lib->getScript(member_number);
    return script ? script->getPropertyNames() : std::vector<std::string>{};
}

lingo::Datum CastLibManager::createMember(int cast_lib_number, const std::string& member_type) {
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib) {
        return lingo::Datum::voidValue();
    }
    auto member = cast_lib->createDynamicMember(member_type);
    if (!member) {
        return lingo::Datum::voidValue();
    }
    return lingo::Datum::castMemberRef(id::CastLibId(cast_lib_number), id::MemberId(member->memberNum()));
}

lingo::Datum CastLibManager::createMember(const std::string& member_name, const std::string& member_type) {
    auto cast_lib = getCastLib(1);
    if (!cast_lib) {
        return lingo::Datum::voidValue();
    }
    auto member = cast_lib->createDynamicMember(member_name, member_type);
    if (!member) {
        return lingo::Datum::voidValue();
    }
    return lingo::Datum::of((cast_lib->number() << 16) | member->memberNum());
}

bool CastLibManager::eraseMember(int cast_lib_number, int member_number) {
    auto member = resolveMember(cast_lib_number, member_number);
    if (!member) {
        return false;
    }
    const bool retired_dynamic_slot = member->isRuntimeDynamic();
    member->erase();
    if (retired_dynamic_slot && member_slot_retired_callback_) {
        member_slot_retired_callback_(cast_lib_number, member_number);
    }
    return true;
}

bool CastLibManager::exchangeMemberMedia(int first_cast_lib_number,
                                         int first_member_number,
                                         int second_cast_lib_number,
                                         int second_member_number) {
    auto first = resolveMember(first_cast_lib_number, first_member_number);
    auto second = resolveMember(second_cast_lib_number, second_member_number);
    if (!first || !second) {
        return false;
    }
    const auto temporary_type = temporaryMemberTypeForExchange(*first, *second);
    if (!temporary_type.has_value()) {
        return false;
    }

    const auto temporary = createMember(first_cast_lib_number, std::string{*temporary_type});
    const auto* temporary_ref = temporary.asCastMemberRef();
    if (temporary_ref == nullptr) {
        return false;
    }

    const auto first_ref = lingo::Datum::CastMemberRef::of(id::CastLibId(first_cast_lib_number),
                                                          id::MemberId(first_member_number));
    const auto second_ref = lingo::Datum::CastMemberRef::of(id::CastLibId(second_cast_lib_number),
                                                           id::MemberId(second_member_number));
    const auto temporary_source_ref = lingo::Datum::CastMemberRef::of(id::CastLibId(temporary_ref->cast_lib),
                                                                    id::MemberId(temporary_ref->memberNum()));
    const bool staged_first = copyMemberMedia(temporary_ref->cast_lib, temporary_ref->memberNum(), first_ref);
    const bool copied_second_to_first = staged_first &&
        copyMemberMedia(first_cast_lib_number, first_member_number, second_ref);
    const bool copied_first_to_second = copied_second_to_first &&
        copyMemberMedia(second_cast_lib_number, second_member_number, temporary_source_ref);
    if (!copied_first_to_second && copied_second_to_first) {
        (void)copyMemberMedia(first_cast_lib_number, first_member_number, temporary_source_ref);
    }
    (void)eraseMember(temporary_ref->cast_lib, temporary_ref->memberNum());
    return staged_first && copied_second_to_first && copied_first_to_second;
}

lingo::Datum CastLibManager::callMemberMethod(int cast_lib_number,
                                              int member_number,
                                              const std::string& method_name,
                                              const std::vector<lingo::Datum>& args) {
    if (equalsIgnoreCase(method_name, "erase")) {
        return eraseMember(cast_lib_number, member_number) ? lingo::Datum::of(1) : lingo::Datum::voidValue();
    }

    auto member = resolveMember(cast_lib_number, member_number);
    if (!member) {
        return lingo::Datum::voidValue();
    }

    if (equalsIgnoreCase(method_name, "duplicate") && !args.empty()) {
        const auto target_ref = duplicateTargetRefFromArg(args.front(), cast_lib_number);
        if (target_ref.has_value()) {
            const int target_cast_lib_number = target_ref->cast_lib > 0 ? target_ref->cast_lib : cast_lib_number;
            auto target_member = resolveMember(target_cast_lib_number, target_ref->memberNum());
            if (!target_member) {
                return lingo::Datum::voidValue();
            }
            if (member->isPalette() && target_member->isPalette()) {
                auto receiver_palette = member->paletteData();
                if (!receiver_palette && !member->isRuntimeDynamic()) {
                    receiver_palette = resolvePaletteByMember(cast_lib_number, member_number);
                }
                if (receiver_palette) {
                    target_member->setPaletteData(receiver_palette);
                    return args.front();
                }
                auto argument_palette = target_member->paletteData();
                if (!argument_palette && !target_member->isRuntimeDynamic()) {
                    argument_palette = resolvePaletteByMember(target_cast_lib_number, target_ref->memberNum());
                }
                if (argument_palette) {
                    member->setPaletteData(argument_palette);
                    return lingo::Datum::castMemberRef(id::CastLibId(cast_lib_number), id::MemberId(member_number));
                }
            }
        }
    }

    if (equalsIgnoreCase(method_name, "getProp")) {
        if (args.size() < 2) {
            return lingo::Datum::voidValue();
        }
        const std::string prop_name = keyNameLikeJava(args[0]);
        const int index = args[1].intValue();
        const lingo::Datum prop_value = getMemberProp(cast_lib_number, member_number, prop_name);
        if (const auto* point = prop_value.asIntPoint()) {
            return lingo::Datum::of(index == 1 ? point->x : point->y);
        }
        if (const auto* rect = prop_value.asIntRect()) {
            switch (index) {
                case 1: return lingo::Datum::of(rect->left);
                case 2: return lingo::Datum::of(rect->top);
                case 3: return lingo::Datum::of(rect->right);
                case 4: return lingo::Datum::of(rect->bottom);
                default: return lingo::Datum::voidValue();
            }
        }
        if (prop_value.isList()) {
            const auto& items = prop_value.listValue().items();
            const int zero_index = index - 1;
            if (zero_index >= 0 && zero_index < static_cast<int>(items.size())) {
                return items[static_cast<std::size_t>(zero_index)];
            }
            return lingo::Datum::voidValue();
        }
        return prop_value;
    }

    if (equalsIgnoreCase(method_name, "count")) {
        if (args.empty() || !member->isTextLike()) {
            return lingo::Datum::of(0);
        }
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        return lingo::Datum::of(countMethodTextChunks(text, keyNameLikeJava(args.front())));
    }

    if (!member->isTextLike()) {
        return lingo::Datum::voidValue();
    }

    if (equalsIgnoreCase(method_name, "charPosToLoc") || equalsIgnoreCase(method_name, "charposttoloc")) {
        if (args.empty() || !text_renderer_) {
            return lingo::Datum::intPoint(0, 0);
        }
        const int char_index = args.front().intValue();
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        if (text.empty() || char_index <= 0) {
            return lingo::Datum::intPoint(0, 0);
        }
        const int field_width = std::max(1, member->textRectRight() - member->textRectLeft());
        const auto loc = charPosToLoc(member->castLib(), member->memberNum(), char_index, field_width);
        if (loc.size() < 2) {
            return lingo::Datum::intPoint(0, 0);
        }
        return lingo::Datum::intPoint(loc[0], loc[1]);
    }

    if (equalsIgnoreCase(method_name, "locToCharPos")) {
        if (args.empty() || !text_renderer_) {
            return lingo::Datum::of(0);
        }
        const auto* point = args.front().asIntPoint();
        const int x = point ? point->x : args.front().intValue();
        const int y = point ? point->y : (args.size() > 1 ? args[1].intValue() : 0);
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        if (text.empty()) {
            return lingo::Datum::of(0);
        }
        const int field_width = std::max(1, member->textRectRight() - member->textRectLeft());
        return lingo::Datum::of(locToCharPos(member->castLib(), member->memberNum(), x, y, field_width));
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::importFileIntoMember(int cast_lib_number,
                                          int member_number,
                                          const std::string& url,
                                          const lingo::Datum& options) {
    (void)options;
    auto cast_lib = getCastLib(cast_lib_number);
    if (!cast_lib || cast_lib->getMember(member_number) == nullptr) {
        return false;
    }

    auto data = getCachedDownloadedData(url);
    if (!data.has_value() || data->empty()) {
        return false;
    }

    auto bitmap = decodeBitmapMedia(*data, cast_lib_number, this);
    if (!bitmap.has_value()) {
        return false;
    }

    auto image = std::make_shared<bitmap::Bitmap>(std::move(*bitmap));
    return cast_lib->setMemberProp(member_number, "image", lingo::Datum::imageRef(std::move(image)));
}

void CastLibManager::cacheExternalData(const std::string& url, const std::vector<std::uint8_t>& data) {
    for (const auto& key : downloadCacheKeys(url)) {
        cast_data_cache_[key] = data;
    }
    for (const auto& cast_lib : findCastLibsByUrl(url)) {
        cast_lib->cacheFetchedExternalData(data);
    }
}

std::optional<std::vector<std::uint8_t>> CastLibManager::getCachedExternalData(const std::string& base_name) const {
    const auto found = cast_data_cache_.find(lower(base_name));
    if (found == cast_data_cache_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::vector<std::uint8_t>> CastLibManager::getCachedDownloadedData(const std::string& url) const {
    for (const auto& key : downloadCacheKeys(url)) {
        if (const auto found = cast_data_cache_.find(key); found != cast_data_cache_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

bool CastLibManager::setExternalCastData(int cast_lib_number, const std::vector<std::uint8_t>& data) {
    ensureInitialized();
    const auto found = cast_libs_.find(cast_lib_number);
    if (found == cast_libs_.end() || !found->second) {
        return false;
    }
    const bool loaded = found->second->setExternalData(data);
    if (loaded) {
        clearPendingExternalLoad(cast_lib_number);
    }
    return loaded;
}

bool CastLibManager::setExternalCastDataByUrl(const std::string& url, const std::vector<std::uint8_t>& data) {
    ensureInitialized();
    bool any_loaded = false;
    for (const auto& cast_lib : findCastLibsByUrl(url)) {
        if (cast_lib->isLoaded() && cast_lib->memberCount() > 0) {
            any_loaded = true;
            continue;
        }
        if (setExternalCastData(cast_lib->number(), data)) {
            any_loaded = true;
        }
    }
    return any_loaded;
}

std::vector<int> CastLibManager::getMatchingCastLibNumbersByUrl(const std::string& url) {
    ensureInitialized();
    const auto base_name = util::getFileNameWithoutExtension(util::getFileName(url));
    std::vector<int> result;
    if (base_name.empty()) {
        return result;
    }
    for (const auto& [_, cast_lib] : cast_libs_) {
        if (cast_lib->matchesAuthoredExternalFile(base_name)) {
            result.push_back(cast_lib->number());
        }
    }
    return result;
}

std::vector<int> CastLibManager::getRequestedExternalCastSlots(const std::string& url) {
    ensureInitialized();
    const auto base_name = util::getFileNameWithoutExtension(util::getFileName(url));
    std::vector<int> result;
    if (base_name.empty()) {
        return result;
    }

    for (const auto& cast_lib : findCastLibsByUrl(url)) {
        const int number = cast_lib->number();
        const auto pending = pending_external_loads_.find(number);
        const bool requested = cast_lib->isFetching() ||
                               (pending != pending_external_loads_.end() && equalsIgnoreCase(base_name, pending->second)) ||
                               (!cast_lib->isLoaded() && cast_lib->matchesAuthoredExternalFile(base_name));
        if (requested) {
            result.push_back(number);
        }
    }
    return result;
}

void CastLibManager::clearPendingExternalLoad(int cast_lib_number) {
    pending_external_loads_.erase(cast_lib_number);
}

void CastLibManager::setMemberSlotRetiredCallback(MemberSlotRetiredCallback callback) {
    member_slot_retired_callback_ = std::move(callback);
}

void CastLibManager::setTextRenderer(render::output::TextRenderer* renderer) {
    text_renderer_ = renderer;
}

void CastLibManager::installBuiltinCallbacks(lingo::builtin::BuiltinContext& context) {
    context.cast_member_creator = [this](int cast_lib, const std::string& member_type) {
        return createMember(cast_lib, member_type);
    };
    context.named_cast_member_creator = [this](const std::string& member_name, const std::string& member_type) {
        return createMember(member_name, member_type);
    };
    context.cast_lib_number_resolver = [this](int cast_lib) {
        return getCastLibByNumber(cast_lib);
    };
    context.cast_lib_name_resolver = [this](const std::string& name) {
        return getCastLibByName(name);
    };
    context.cast_lib_count_supplier = [this]() {
        return getCastLibCount();
    };
    context.cast_lib_property_getter = [this](int cast_lib, const std::string& property_name) {
        return getCastLibProp(cast_lib, property_name);
    };
    context.cast_lib_property_setter = [this](int cast_lib, const std::string& property_name, const lingo::Datum& value) {
        return setCastLibProp(cast_lib, property_name, value);
    };
    context.cast_member_count_supplier = [this](int cast_lib) {
        return getMemberCount(cast_lib);
    };
    context.cast_member_resolver = [this](int cast_lib, int member_num) {
        return getMember(cast_lib, member_num);
    };
    context.cast_member_name_resolver = [this](int cast_lib, const std::string& member_name) {
        return getMemberByName(cast_lib, member_name);
    };
    context.cast_member_exists_resolver = [this](int cast_lib, int member_num) {
        return memberExists(cast_lib, member_num);
    };
    context.registry_visible_member_resolver = [this](int cast_lib, int member_num) {
        return isRegistryVisibleMember(cast_lib, member_num);
    };
    context.cast_member_method_handler = [this](int cast_lib,
                                             int member_num,
                                             const std::string& method_name,
                                             const std::vector<lingo::Datum>& args) {
        return callMemberMethod(cast_lib, member_num, method_name, args);
    };
    context.cast_member_property_getter = [this](int cast_lib, int member_num, const std::string& property_name) {
        return getMemberProp(cast_lib, member_num, property_name);
    };
    context.cast_member_property_setter = [this](int cast_lib, int member_num, const std::string& property_name, const lingo::Datum& value) {
        return setMemberProp(cast_lib, member_num, property_name, value);
    };
    context.field_resolver = [this](const lingo::Datum& identifier, int cast_lib) {
        return getFieldDatum(identifier, cast_lib);
    };
    context.field_parsed_value_resolver = [this](int cast_lib, int member_num, std::uint64_t revision) {
        return getParsedFieldValue(cast_lib, member_num, revision);
    };
    context.field_setter = [this](const lingo::Datum& identifier, int cast_lib, const std::string& value) {
        setFieldValue(identifier, cast_lib, value);
    };
    context.script_chunk_id_resolver = [this](int cast_lib, int member_num) {
        return getScriptChunkId(cast_lib, member_num);
    };
    context.script_property_names_resolver = [this](int cast_lib, int member_num) {
        return getScriptPropertyNames(cast_lib, member_num);
    };
    context.import_file_into_handler = [this](const lingo::Datum::CastMemberRef& ref,
                                           const std::string& url,
                                           const lingo::Datum& options) {
        return importFileIntoMember(ref.cast_lib > 0 ? ref.cast_lib : 1, ref.memberNum(), url, options);
    };
}

void CastLibManager::ensureInitialized() {
    if (initialized_ || !file_) {
        return;
    }
    initialized_ = true;

    auto cast_list = file_->castList();
    const auto& casts = file_->casts();
    const auto base_path = file_->basePath();
    (void)base_path;

    if (cast_list && !cast_list->entries().empty()) {
        for (int index = 0; index < static_cast<int>(cast_list->entries().size()); ++index) {
            const int cast_lib_number = index + 1;
            const auto& list_entry = cast_list->entries()[static_cast<std::size_t>(index)];
            const bool is_external = !list_entry.path.empty();
            std::shared_ptr<chunks::CastChunk> cast_chunk;
            if (!is_external) {
                cast_chunk = file_->getMappedCastChunk(cast_lib_number);
            } else if (index < static_cast<int>(casts.size())) {
                cast_chunk = casts[static_cast<std::size_t>(index)];
            }

            auto cast_lib = std::make_shared<CastLib>(cast_lib_number, cast_chunk, &list_entry);
            cast_lib->setBitmapPaletteHostFile(file_);
            cast_lib->setPreloadMode(list_entry.preload_settings);
            if (!is_external) {
                cast_lib->setSourceFile(file_);
            }
            cast_libs_[cast_lib_number] = cast_lib;
        }
    } else if (!casts.empty()) {
        for (int index = 0; index < static_cast<int>(casts.size()); ++index) {
            const int cast_lib_number = index + 1;
            auto cast_lib = std::make_shared<CastLib>(cast_lib_number, casts[static_cast<std::size_t>(index)], nullptr);
            cast_lib->setBitmapPaletteHostFile(file_);
            cast_lib->setSourceFile(file_);
            cast_libs_[cast_lib_number] = cast_lib;
        }
    }
}

void CastLibManager::tryLoadCastFromCache(int cast_lib_number, const std::string& new_file_name) {
    if (new_file_name.empty()) {
        return;
    }
    markPendingExternalLoad(cast_lib_number, new_file_name);
    if (cast_data_request_callback_) {
        cast_data_request_callback_(cast_lib_number, new_file_name);
    }
}

void CastLibManager::markPendingExternalLoad(int cast_lib_number, const std::string& file_name) {
    pending_external_loads_[cast_lib_number] = util::getFileNameWithoutExtension(util::getFileName(file_name));
}

std::vector<std::shared_ptr<CastLib>> CastLibManager::findCastLibsByUrl(const std::string& url) {
    ensureInitialized();
    const auto extracted_file_name = util::getFileName(url);
    const auto file_name_no_ext = util::getFileNameWithoutExtension(extracted_file_name);
    std::vector<std::shared_ptr<CastLib>> result;
    if (file_name_no_ext.empty()) {
        return result;
    }

    for (const auto& [_, cast_lib] : cast_libs_) {
        const auto cast_path = cast_lib->fileName();
        if (cast_path.empty()) {
            continue;
        }
        const auto cast_file_no_ext = util::getFileNameWithoutExtension(util::getFileName(cast_path));
        if (equalsIgnoreCase(cast_file_no_ext, file_name_no_ext)) {
            result.push_back(cast_lib);
        }
    }
    return result;
}

std::vector<std::string> CastLibManager::downloadCacheKeys(const std::string& url) {
    std::vector<std::string> keys;
    if (url.empty()) {
        return keys;
    }
    const auto file_name = util::getFileName(url);
    if (file_name.empty()) {
        return keys;
    }
    keys.push_back(lower(file_name));
    const auto base_name = util::getFileNameWithoutExtension(file_name);
    if (!base_name.empty()) {
        const auto lowered_base = lower(base_name);
        if (std::find(keys.begin(), keys.end(), lowered_base) == keys.end()) {
            keys.push_back(lowered_base);
        }
    }
    return keys;
}

bool CastLibManager::copyMemberMedia(int target_cast_lib_number,
                                     int target_member_number,
                                     const lingo::Datum::CastMemberRef& source_ref) {
    auto target = resolveMember(target_cast_lib_number, target_member_number);
    const int source_cast_lib = source_ref.cast_lib > 0 ? source_ref.cast_lib : target_cast_lib_number;
    auto source = resolveMember(source_cast_lib, source_ref.memberNum());
    if (!target || !source) {
        return false;
    }

    if (target->isTextLike() && source->isTextLike()) {
        const auto source_cast = getCastLib(source_cast_lib);
        const auto source_text = getMemberProp(source->castLib(), source->memberNum(), "text").stringValue();
        target->setDynamicText(source_text);
        if (const auto styled_text = authoredXmedText(source_cast, source)) {
            applyTextStyleFromXmed(*target, *styled_text);
        } else {
            target->setTextFont(source->textFont());
            target->setTextFontSize(source->textFontSize());
            target->setTextFontStyle(source->textFontStyle());
            target->setTextAlignment(source->textAlignment());
            target->setTextColor(source->textColor());
            target->setTextWordWrap(source->textWordWrap());
            target->setTextAntialias(source->textAntialias());
            target->setTextRect(source->textRectLeft(),
                                source->textRectTop(),
                                source->textRectRight(),
                                source->textRectBottom());
            target->setTextFixedLineSpace(source->textFixedLineSpace());
        }
        target->setTextBgColor(source->textBgColor());
        target->setTextBoxType(source->textBoxType());
        target->setTextTopSpacing(source->textTopSpacing());
        target->setTextGutterSize(source->textGutterSize());
        target->setEditable(source->editable());
        return true;
    }

    if (target->isBitmap() && source->isBitmap()) {
        if (auto bitmap = source->runtimeBitmap()) {
            if (source->rawChunk()) {
                const auto image = getMemberProp(source->castLib(), source->memberNum(), "image");
                if (const auto* image_ref = image.asImageRef()) {
                    bitmap = image_ref->bitmap;
                }
                if (!source->runtimePaletteOverride()) {
                    const auto info = source->bitmapInfo();
                    if (info.has_value() && bitmap::Palette::builtInSymbolName(info->palette_id).has_value()) {
                        BitmapResolver resolver(file_, this, nullptr);
                        if (auto authored = resolver.decodeBitmap(source->rawChunk())) {
                            bitmap = std::make_shared<bitmap::Bitmap>(std::move(*authored));
                        }
                    }
                }
            }
            target->setRegPointState(source->regX(), source->regY(), source->regPointPinnedToMember());
            target->setBitmapAlphaThreshold(source->bitmapAlphaThreshold());
            target->setRuntimeBitmap(*bitmap);
            target->setRuntimePaletteOverride(source->runtimePaletteOverride(),
                                              source->paletteRefCastLib(),
                                              source->paletteRefMemberNum(),
                                              source->paletteRefSystemName(),
                                              false);
            return true;
        }
        if (source->rawChunk()) {
            ::libreshockwave::player::BitmapResolver resolver(file_, this, nullptr);
            if (auto bitmap = resolver.decodeBitmap(source->rawChunk())) {
                bitmap->setAnchorPoint(source->regX(), source->regY());
                target->setRegPointState(source->regX(), source->regY(), source->regPointPinnedToMember());
                target->setBitmapAlphaThreshold(source->bitmapAlphaThreshold());
                target->setRuntimeBitmap(*bitmap);
                target->setRuntimePaletteOverride(source->runtimePaletteOverride(),
                                                  source->paletteRefCastLib(),
                                                  source->paletteRefMemberNum(),
                                                  source->paletteRefSystemName(),
                                                  false);
                return true;
            }
        }
    }
    if (target->isPalette() && source->isPalette()) {
        auto palette = resolvePaletteByMember(source_cast_lib, source->memberNum());
        if (!palette) {
            return false;
        }
        target->setPaletteData(clonePalette(*palette));
        return true;
    }
    if (target->isScript() && source->isScript()) {
        auto source_cast_lib_object = getCastLib(source_cast_lib);
        auto script = source_cast_lib_object ? source_cast_lib_object->getScript(source->memberNum()) : nullptr;
        if (!script) {
            return false;
        }
        target->setRuntimeScript(std::move(script));
        return true;
    }
    return false;
}

lingo::Datum CastLibManager::getMemberByNameInCast(const std::shared_ptr<CastLib>& cast_lib,
                                                   const std::string& member_name) {
    if (!cast_lib || member_name.empty()) {
        return lingo::Datum::voidValue();
    }
    auto find_by_name = [&](const std::string& name) -> lingo::Datum {
        if (auto member = cast_lib->findMemberByName(name)) {
            const int member_number = cast_lib->getMemberNumber(member);
            if (member_number >= 0) {
                return lingo::Datum::castMemberRef(id::CastLibId(cast_lib->number()), id::MemberId(member_number));
            }
        }
        if (auto member = cast_lib->getMemberByName(name)) {
            return lingo::Datum::castMemberRef(id::CastLibId(cast_lib->number()), id::MemberId(member->memberNum()));
        }
        return lingo::Datum::voidValue();
    };
    auto found = find_by_name(member_name);
    if (!found.isVoid()) {
        return found;
    }
    if (const auto normalized = normalizeUnderscoreNumericTokens(member_name)) {
        return find_by_name(*normalized);
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::isRegistryFallbackEligibleCast(const std::shared_ptr<CastLib>& cast_lib) {
    return cast_lib != nullptr && cast_lib->usesStableRegistryBinding();
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::resolveFieldMember(
    const lingo::Datum& identifier,
    int cast_lib_number) {
    ensureInitialized();

    if (identifier.isInt()) {
        const int raw_number = identifier.intValue();
        int effective_cast_lib = cast_lib_number > 0 ? cast_lib_number : 1;
        int effective_member_number = raw_number;
        if (raw_number > 0xFFFF) {
            effective_cast_lib = (raw_number >> 16) & 0xFFFF;
            effective_member_number = raw_number & 0xFFFF;
        }
        return resolveMember(effective_cast_lib, effective_member_number);
    }

    const std::string member_name = identifier.stringValue();
    if (member_name.empty()) {
        return nullptr;
    }

    if (cast_lib_number > 0) {
        auto found = getMemberByNameInCast(getCastLib(cast_lib_number), member_name);
        if (const auto* member_ref = found.asCastMemberRef()) {
            return resolveMember(member_ref->cast_lib, member_ref->memberNum());
        }
        return nullptr;
    }

    for (const auto& [_, cast_lib] : cast_libs_) {
        auto found = getMemberByNameInCast(getCastLib(cast_lib->number()), member_name);
        if (const auto* member_ref = found.asCastMemberRef()) {
            return resolveMember(member_ref->cast_lib, member_ref->memberNum());
        }
    }
    return nullptr;
}

} // namespace libreshockwave::player::cast
