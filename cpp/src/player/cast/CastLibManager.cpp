#include "libreshockwave/player/cast/CastLibManager.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/BitmapDecoder.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/bitmap/PngDecoder.hpp"
#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/XmedStyledText.hpp"
#include "libreshockwave/chunks/CastChunk.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/lingo/LingoValueParser.hpp"
#include "libreshockwave/player/BitmapResolver.hpp"
#include "libreshockwave/player/render/output/TextRenderer.hpp"
#include "libreshockwave/util/FileUtil.hpp"

namespace libreshockwave::player::cast {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lower(lhs) == lower(rhs);
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
            std::size_t firstNonZero = 0;
            while (firstNonZero + 1 < token.size() && token[firstNonZero] == '0') {
                ++firstNonZero;
            }
            if (firstNonZero > 0) {
                changed = true;
            }
            normalized.append(token.substr(firstNonZero));
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

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {});
}

struct ResolvedPaletteOverride {
    std::shared_ptr<const bitmap::Palette> palette;
    std::optional<lingo::Datum::CastMemberRef> memberRef;
    std::optional<std::string> systemName;
};

std::optional<ResolvedPaletteOverride> resolvePaletteMemberOverrideByName(CastLibManager& manager,
                                                                           const std::string& name) {
    for (const auto& [_, castLib] : manager.castLibs()) {
        if (!castLib) {
            continue;
        }
        auto member = castLib->getPaletteMemberByName(name);
        if (!member) {
            continue;
        }
        if (auto palette = manager.resolvePaletteByMember(castLib->number(), member->memberNum())) {
            return ResolvedPaletteOverride{
                palette,
                lingo::Datum::CastMemberRef::of(id::CastLibId(castLib->number()), id::MemberId(member->memberNum())),
                std::nullopt
            };
        }
    }
    return std::nullopt;
}

std::optional<ResolvedPaletteOverride> resolvePaletteOverrideDatum(CastLibManager& manager,
                                                                   const lingo::Datum& value,
                                                                   int defaultCastLib) {
    if (const auto* ref = value.asCastMemberRef()) {
        const int castLib = ref->castLib > 0 ? ref->castLib : defaultCastLib;
        if (auto palette = manager.resolvePaletteByMember(castLib, ref->memberNum())) {
            return ResolvedPaletteOverride{
                palette,
                lingo::Datum::CastMemberRef::of(id::CastLibId(castLib), id::MemberId(ref->memberNum())),
                std::nullopt
            };
        }
    }

    if (value.isString() || value.isSymbol()) {
        const std::string name = keyNameLikeJava(value);
        if (const auto* builtIn = bitmap::Palette::builtInBySymbolName(name)) {
            const auto normalized = bitmap::Palette::normalizeBuiltInSymbolName(name);
            return ResolvedPaletteOverride{borrowedPalette(builtIn), std::nullopt, normalized};
        }

        if (auto namedPalette = resolvePaletteMemberOverrideByName(manager, name)) {
            return namedPalette;
        }

        if (auto palette = manager.resolvePaletteByName(name)) {
            return ResolvedPaletteOverride{palette, std::nullopt, std::nullopt};
        }
    }

    return std::nullopt;
}

int countMethodWords(std::string_view text) {
    int count = 0;
    bool inWord = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            inWord = false;
        } else if (!inWord) {
            ++count;
            inWord = true;
        }
    }
    return count;
}

int countMethodTextChunks(std::string_view text, const std::string& chunkType) {
    if (text.empty()) {
        return 0;
    }
    const std::string type = lower(chunkType);
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
                         int rectWidth) {
    int maxWidth = std::max(1, rectWidth);
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
            maxWidth = std::max(maxWidth, loc[0] + 2);
        }
    }
    return maxWidth;
}

std::shared_ptr<bitmap::Bitmap> renderTextMemberImage(
    CastLib& castLib,
    const std::shared_ptr<libreshockwave::cast::CastMember>& member,
    render::output::TextRenderer* renderer) {
    if (!member || !member->isTextLike() || renderer == nullptr) {
        return nullptr;
    }

    const std::string text = castLib.getMemberProp(member->memberNum(), "text").stringValue();
    const int rectWidth = member->textRectRight() - member->textRectLeft();
    int width = rectWidth;
    if (member->textBoxType() == 0 && !member->textWordWrap()) {
        width = std::max(width, measureAutoTextWidth(member, renderer, text, rectWidth));
    }
    const int height = member->textBoxType() == 0 ? 0 : member->textRectBottom() - member->textRectTop();
    auto rendered = renderer->renderText(text,
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
                                         member->textTopSpacing());
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

    const auto rawWidth = readU32BE(data, 4);
    const auto rawHeight = readU32BE(data, 8);
    if (rawWidth == 0 ||
        rawHeight == 0 ||
        rawWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        rawHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const std::size_t width = rawWidth;
    const std::size_t height = rawHeight;
    if (width > (std::numeric_limits<std::size_t>::max() / height) ||
        width * height > (std::numeric_limits<std::size_t>::max() / 4U)) {
        return std::nullopt;
    }

    const std::size_t pixelBytes = width * height * 4U;
    if (data.size() < 12U + pixelBytes) {
        return std::nullopt;
    }

    bitmap::Bitmap bitmap(static_cast<int>(width), static_cast<int>(height), 32);
    bool hasAlpha = false;
    std::size_t offset = 12;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const int r = data[offset++];
            const int g = data[offset++];
            const int b = data[offset++];
            const int a = data[offset++];
            if (a < 255) {
                hasAlpha = true;
            }
            bitmap.setPixel(x,
                            y,
                            (static_cast<std::uint32_t>(a & 0xFF) << 24U) |
                                (static_cast<std::uint32_t>(r & 0xFF) << 16U) |
                                (static_cast<std::uint32_t>(g & 0xFF) << 8U) |
                                static_cast<std::uint32_t>(b & 0xFF));
        }
    }
    if (hasAlpha) {
        bitmap.setNativeAlpha(true);
    }
    return bitmap;
}

std::shared_ptr<const bitmap::Palette> resolveMediaPalette(const ::libreshockwave::cast::BitmapInfo& info,
                                                           int defaultCastLib,
                                                           CastLibManager* manager) {
    if (info.paletteId < 0) {
        return std::shared_ptr<const bitmap::Palette>(
            &bitmap::Palette::builtIn(info.paletteId),
            [](const bitmap::Palette*) {});
    }

    const int memberNumber = info.paletteId + 1;
    if (manager != nullptr) {
        if (info.paletteCastLib > 0) {
            if (auto palette = manager->resolvePaletteById(info.paletteCastLib, info.paletteId)) {
                return palette;
            }
            if (auto palette = manager->resolvePaletteByMember(info.paletteCastLib, memberNumber)) {
                return palette;
            }
        }
        if (defaultCastLib > 0) {
            if (auto palette = manager->resolvePaletteById(defaultCastLib, info.paletteId)) {
                return palette;
            }
            if (auto palette = manager->resolvePaletteByMember(defaultCastLib, memberNumber)) {
                return palette;
            }
        }
    }
    return std::shared_ptr<const bitmap::Palette>(
        &bitmap::Palette::systemMacPalette(),
        [](const bitmap::Palette*) {});
}

std::optional<bitmap::Bitmap> decodeDirectorBitmapMedia(const std::vector<std::uint8_t>& data,
                                                        int defaultCastLib,
                                                        CastLibManager* manager) {
    const auto marker = findBitdMarker(data);
    if (!marker.has_value() || *marker < 32 || *marker + 8 > data.size()) {
        return std::nullopt;
    }

    const auto bitdLen = readU32LE(data, *marker + 4);
    const std::size_t bitdStart = *marker + 8;
    if (bitdLen == 0 || bitdLen > data.size() - bitdStart) {
        return std::nullopt;
    }

    const std::vector<std::uint8_t> infoData(data.begin() + static_cast<std::ptrdiff_t>(*marker - 32),
                                             data.begin() + static_cast<std::ptrdiff_t>(*marker));
    const auto info = ::libreshockwave::cast::BitmapInfo::parse(infoData, 1200);
    if (info.width <= 0 || info.height <= 0) {
        return std::nullopt;
    }

    const std::vector<std::uint8_t> bitd(data.begin() + static_cast<std::ptrdiff_t>(bitdStart),
                                         data.begin() + static_cast<std::ptrdiff_t>(bitdStart + bitdLen));
    std::shared_ptr<const bitmap::Palette> palette;
    if (info.bitDepth <= 8) {
        palette = resolveMediaPalette(info, defaultCastLib, manager);
    }

    auto bitmap = bitmap::BitmapDecoder::decode(bitd,
                                                info.width,
                                                info.height,
                                                info.bitDepth,
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
                                                int defaultCastLib,
                                                CastLibManager* manager) {
    auto bitmap = decodeImportedImage(data);
    if (!bitmap.has_value()) {
        bitmap = bitmap::PngDecoder::decode(data);
    }
    if (!bitmap.has_value()) {
        bitmap = decodeDirectorBitmapMedia(data, defaultCastLib, manager);
    }
    return bitmap;
}

std::shared_ptr<const bitmap::Palette> clonePalette(const bitmap::Palette& palette) {
    return std::make_shared<bitmap::Palette>(palette.colors(), palette.name());
}

std::optional<::libreshockwave::cast::XmedStyledText> authoredXmedText(
    const std::shared_ptr<CastLib>& castLib,
    const std::shared_ptr<::libreshockwave::cast::CastMember>& member) {
    if (!castLib || !member || !member->rawChunk()) {
        return std::nullopt;
    }
    const auto sourceFile = castLib->sourceFile();
    if (!sourceFile) {
        return std::nullopt;
    }
    return sourceFile->getXmedStyledTextForMember(member->rawChunk());
}

void applyTextStyleFromXmed(::libreshockwave::cast::CastMember& target,
                            const ::libreshockwave::cast::XmedStyledText& styled) {
    if (!styled.fontName.empty()) {
        target.setTextFont(styled.fontName);
    }
    if (styled.fontSize > 0) {
        target.setTextFontSize(styled.fontSize);
    }
    target.setTextFontStyle(styled.fontStyleString().empty() ? "plain" : styled.fontStyleString());
    if (!styled.alignment.empty()) {
        target.setTextAlignment(styled.alignment);
    }
    target.setTextColor(static_cast<int>(styled.textColorARGB()));
    target.setTextWordWrap(styled.wordWrap);
    target.setTextAntialias(styled.antialias);
    if (styled.width > 0 && styled.height > 0) {
        target.setTextRect(0, 0, styled.width, styled.height);
    }
    target.setTextFixedLineSpace(styled.fixedLineSpace);
}

std::shared_ptr<const bitmap::Palette> paletteFromCastMember(const std::shared_ptr<CastLib>& castLib,
                                                             int memberNumber) {
    if (!castLib) {
        return nullptr;
    }
    if (!castLib->isLoaded()) {
        castLib->load();
    }
    auto member = castLib->getMember(memberNumber);
    if (member && member->isPalette()) {
        if (auto palette = member->paletteData()) {
            return palette;
        }
    }
    if (castLib->sourceFile() != nullptr) {
        return castLib->sourceFile()->resolvePaletteByMemberNumber(memberNumber);
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> paletteFromCastById(const std::shared_ptr<CastLib>& castLib,
                                                           int paletteId) {
    if (!castLib) {
        return nullptr;
    }

    auto source = castLib->sourceFile();
    if (source) {
        if (auto palette = source->resolvePaletteExact(paletteId)) {
            return palette;
        }
    }

    if (!castLib->isLoaded()) {
        castLib->load();
    }

    const int memberNumber = paletteId + 1;
    auto member = castLib->getMember(memberNumber);
    if (member && member->isPalette()) {
        if (auto palette = member->paletteData()) {
            return palette;
        }
    }

    auto chunk = castLib->findMemberByNumber(memberNumber);
    if (chunk && source) {
        return source->resolvePaletteByMemberNumber(memberNumber);
    }
    return nullptr;
}

std::optional<lingo::Datum::CastMemberRef> duplicateTargetRefFromArg(const lingo::Datum& targetArg,
                                                                     int defaultCastLib) {
    if (const auto* targetRef = targetArg.asCastMemberRef()) {
        return *targetRef;
    }
    if (!targetArg.isNumber()) {
        return std::nullopt;
    }

    const int slotValue = targetArg.intValue();
    const id::SlotId slotId(slotValue);
    if (slotId.castLib() >= 1 && slotId.member() >= 1) {
        return lingo::Datum::CastMemberRef::of(id::CastLibId(slotId.castLib()),
                                               id::MemberId(slotId.member()));
    }
    if (defaultCastLib >= 1 && slotValue >= 1) {
        return lingo::Datum::CastMemberRef::of(id::CastLibId(defaultCastLib), id::MemberId(slotValue));
    }
    return std::nullopt;
}

} // namespace

CastLibManager::CastLibManager(std::shared_ptr<DirectorFile> file,
                               CastDataRequestCallback castDataRequestCallback)
    : file_(std::move(file)),
      castDataRequestCallback_(std::move(castDataRequestCallback)) {}

std::shared_ptr<DirectorFile> CastLibManager::file() const {
    return file_;
}

std::shared_ptr<CastLib> CastLibManager::getCastLib(int castLibNumber) {
    ensureInitialized();
    if (const auto found = castLibs_.find(castLibNumber); found != castLibs_.end()) {
        if (found->second && !found->second->isLoaded()) {
            found->second->load();
        }
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<CastLib> CastLibManager::getCastLibByNameInternal(const std::string& name) {
    ensureInitialized();
    for (const auto& [_, castLib] : castLibs_) {
        if (castLib && equalsIgnoreCase(castLib->name(), name)) {
            if (!castLib->isLoaded()) {
                castLib->load();
            }
            return castLib;
        }
    }
    if (equalsIgnoreCase(name, "internal")) {
        return getCastLib(1);
    }
    return nullptr;
}

int CastLibManager::getCastLibByNumber(int castLibNumber) {
    return getCastLib(castLibNumber) ? castLibNumber : -1;
}

int CastLibManager::getCastLibByName(const std::string& name) {
    auto castLib = getCastLibByNameInternal(name);
    return castLib ? castLib->number() : -1;
}

int CastLibManager::getCastLibCount() {
    ensureInitialized();
    return static_cast<int>(castLibs_.size());
}

std::map<int, std::shared_ptr<CastLib>>& CastLibManager::castLibs() {
    ensureInitialized();
    return castLibs_;
}

lingo::Datum CastLibManager::getCastLibProp(int castLibNumber, const std::string& propName) {
    auto castLib = getCastLib(castLibNumber);
    return castLib ? castLib->getProp(propName) : lingo::Datum::voidValue();
}

bool CastLibManager::setCastLibProp(int castLibNumber, const std::string& propName, const lingo::Datum& value) {
    auto castLib = getCastLib(castLibNumber);
    if (!castLib) {
        return false;
    }
    const bool result = castLib->setProp(propName, value);
    if (result && equalsIgnoreCase(propName, "filename")) {
        tryLoadCastFromCache(castLibNumber, value.stringValue());
    }
    return result;
}

std::string CastLibManager::getCastLibName(int castLibNumber) {
    ensureInitialized();
    if (const auto found = castLibs_.find(castLibNumber); found != castLibs_.end() && found->second) {
        return found->second->name();
    }
    return "";
}

std::string CastLibManager::getCastLibFileName(int castLibNumber) {
    ensureInitialized();
    if (const auto found = castLibs_.find(castLibNumber); found != castLibs_.end() && found->second) {
        return found->second->fileName();
    }
    return "";
}

bool CastLibManager::fetchCastLib(int castLibNumber) {
    ensureInitialized();
    if (const auto found = castLibs_.find(castLibNumber); found != castLibs_.end() && found->second) {
        return found->second->isFetched();
    }
    return false;
}

bool CastLibManager::isCastLibExternal(int castLibNumber) {
    ensureInitialized();
    if (const auto found = castLibs_.find(castLibNumber); found != castLibs_.end() && found->second) {
        return found->second->isExternal();
    }
    return false;
}

void CastLibManager::preloadCasts(int mode) {
    ensureInitialized();
    for (const auto& [_, castLib] : castLibs_) {
        if (castLib && castLib->preloadMode() == mode && castLib->isFetched() && !castLib->isLoaded()) {
            castLib->load();
        }
    }
}

lingo::Datum CastLibManager::getMember(int castLibNumber, int memberNumber) {
    if (castLibNumber < 1 || memberNumber < 0) {
        return lingo::Datum::voidValue();
    }
    (void)getCastLib(castLibNumber);
    return lingo::Datum::castMemberRef(id::CastLibId(castLibNumber), id::MemberId(memberNumber));
}

lingo::Datum CastLibManager::getMemberByName(int castLibNumber, const std::string& memberName) {
    ensureInitialized();
    if (castLibNumber > 0) {
        return getMemberByNameInCast(getCastLib(castLibNumber), memberName);
    }

    for (const auto& [_, castLib] : castLibs_) {
        if (!isRegistryFallbackEligibleCast(castLib)) {
            continue;
        }
        auto loadedCast = getCastLib(castLib->number());
        auto found = getMemberByNameInCast(loadedCast, memberName);
        if (!found.isVoid()) {
            return found;
        }
    }

    for (const auto& [_, castLib] : castLibs_) {
        auto found = getMemberByNameInCast(getCastLib(castLib->number()), memberName);
        if (!found.isVoid()) {
            return found;
        }
    }
    return lingo::Datum::voidValue();
}

lingo::Datum CastLibManager::getRegistryMemberByName(int castLibNumber, const std::string& memberName) {
    ensureInitialized();
    if (castLibNumber > 0) {
        const auto found = castLibs_.find(castLibNumber);
        auto castLib = found == castLibs_.end() ? nullptr : found->second;
        if (!isRegistryFallbackEligibleCast(castLib)) {
            return lingo::Datum::voidValue();
        }
        return getMemberByNameInCast(getCastLib(castLibNumber), memberName);
    }

    for (const auto& [_, castLib] : castLibs_) {
        if (!isRegistryFallbackEligibleCast(castLib)) {
            continue;
        }
        auto loadedCast = getCastLib(castLib->number());
        auto found = getMemberByNameInCast(loadedCast, memberName);
        if (!found.isVoid()) {
            return found;
        }
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::memberExists(int castLibNumber, int memberNumber) {
    auto castLib = getCastLib(castLibNumber);
    return castLib && castLib->hasMemberNumber(memberNumber);
}

bool CastLibManager::isRegistryVisibleMember(int castLibNumber, int memberNumber) {
    if (memberNumber <= 0) {
        return false;
    }
    auto castLib = getCastLib(castLibNumber);
    return isRegistryFallbackEligibleCast(castLib) && castLib->getMember(memberNumber) != nullptr;
}

int CastLibManager::getMemberCount(int castLibNumber) {
    auto castLib = getCastLib(castLibNumber);
    return castLib ? castLib->memberCount() : 0;
}

lingo::Datum CastLibManager::getMemberProp(int castLibNumber, int memberNumber, const std::string& propName) {
    auto castLib = getCastLib(castLibNumber);
    if (!castLib) {
        return CastLib::getInvalidMemberProp(propName);
    }

    const auto prop = lower(propName);
    if (auto member = castLib->getMember(memberNumber)) {
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
                const auto value = castLib->getMemberProp(memberNumber, propName);
                return lingo::Datum::fieldText(value.stringValue(), member->castLib(), member->memberNum(), member->textRevision() + 1);
            }
            if (prop == "image") {
                auto rendered = renderTextMemberImage(*castLib, member, textRenderer_);
                return rendered
                    ? lingo::Datum::imageRef(std::move(rendered))
                    : castLib->getMemberProp(memberNumber, propName);
            }
            if ((prop == "height" || prop == "rect") && member->textBoxType() == 0 && textRenderer_ != nullptr) {
                auto rendered = renderTextMemberImage(*castLib, member, textRenderer_);
                if (rendered) {
                    const int rectHeight = member->textRectBottom() - member->textRectTop();
                    if (prop == "height") {
                        if (member->isRuntimeDynamic() && rectHeight >= 256) {
                            return lingo::Datum::of(rendered->height());
                        }
                        return lingo::Datum::of(std::max(rendered->height(), rectHeight));
                    }
                    if (rendered->height() > rectHeight) {
                        return lingo::Datum::intRect(member->textRectLeft(),
                                                     member->textRectTop(),
                                                     member->textRectRight(),
                                                     member->textRectTop() + rendered->height());
                    }
                }
            }
        }
    }
    return castLib->getMemberProp(memberNumber, propName);
}

bool CastLibManager::setMemberProp(int castLibNumber,
                                   int memberNumber,
                                   const std::string& propName,
                                   const lingo::Datum& value) {
    if (equalsIgnoreCase(propName, "paletteRef") || equalsIgnoreCase(propName, "palette")) {
        auto member = resolveMember(castLibNumber, memberNumber);
        if (!member || !member->isBitmap()) {
            return false;
        }
        auto resolved = resolvePaletteOverrideDatum(*this, value, castLibNumber);
        if (!resolved.has_value() || !resolved->palette) {
            return false;
        }

        int paletteRefCastLib = -1;
        int paletteRefMemberNum = -1;
        if (resolved->memberRef.has_value()) {
            paletteRefCastLib = resolved->memberRef->castLib;
            paletteRefMemberNum = resolved->memberRef->memberNum();
        }
        member->setRuntimePaletteOverride(resolved->palette,
                                          paletteRefCastLib,
                                          paletteRefMemberNum,
                                          resolved->systemName,
                                          equalsIgnoreCase(propName, "palette"));
        return true;
    }
    if (equalsIgnoreCase(propName, "media")) {
        if (const auto* sourceRef = value.asCastMemberRef()) {
            return copyMemberMedia(castLibNumber, memberNumber, *sourceRef);
        }
        if (const auto* media = value.asMedia()) {
            auto member = resolveMember(castLibNumber, memberNumber);
            if (!member || !member->isBitmap()) {
                return false;
            }
            auto bitmap = decodeBitmapMedia(media->bytes, castLibNumber, this);
            if (!bitmap.has_value()) {
                return false;
            }
            auto image = std::make_shared<bitmap::Bitmap>(std::move(*bitmap));
            auto castLib = getCastLib(castLibNumber);
            return castLib && castLib->setMemberProp(memberNumber, "image", lingo::Datum::imageRef(std::move(image)));
        }
    }
    auto castLib = getCastLib(castLibNumber);
    return castLib && castLib->setMemberProp(memberNumber, propName, value);
}

lingo::Datum CastLibManager::getFieldValue(const lingo::Datum& identifier, int castLibNumber) {
    auto member = resolveFieldMember(identifier, castLibNumber);
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

lingo::Datum CastLibManager::getFieldDatum(const lingo::Datum& identifier, int castLibNumber) {
    auto member = resolveFieldMember(identifier, castLibNumber);
    if (!member) {
        return lingo::Datum::of(std::string());
    }
    lingo::Datum text = getFieldValue(identifier, castLibNumber);
    return lingo::Datum::fieldText(text.stringValue(), member->castLib(), member->memberNum(), member->textRevision() + 1);
}

lingo::Datum CastLibManager::getParsedFieldValue(int castLibNumber, int memberNumber, std::uint64_t revision) {
    const auto key = std::make_pair(castLibNumber, memberNumber);
    if (revision != 0) {
        if (const auto found = parsedFieldCache_.find(key);
            found != parsedFieldCache_.end() && found->second.revision == revision) {
            return found->second.value;
        }
    }

    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member || !member->isTextLike()) {
        return lingo::Datum::voidValue();
    }

    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    if (const auto found = parsedFieldCache_.find(key);
        found != parsedFieldCache_.end() && found->second.text == text) {
        return found->second.value;
    }

    lingo::Datum parsed = lingo::LingoValueParser::parseWithPartial(text);
    parsedFieldCache_[key] = ParsedFieldCacheEntry{revision, text, parsed};
    return parsed;
}

std::vector<int> CastLibManager::charPosToLoc(int castLibNumber, int memberNumber, int charIndex, int fieldWidth) {
    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member || !member->isTextLike() || !textRenderer_) {
        return {};
    }
    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    return textRenderer_->charPosToLoc(text,
                                       charIndex,
                                       member->textFont(),
                                       member->textFontSize(),
                                       member->textFontStyle(),
                                       member->textFixedLineSpace(),
                                       member->textAlignment(),
                                       std::max(1, fieldWidth));
}

int CastLibManager::locToCharPos(int castLibNumber, int memberNumber, int x, int y, int fieldWidth) {
    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member || !member->isTextLike() || !textRenderer_) {
        return 0;
    }
    const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    if (text.empty()) {
        return 0;
    }
    return textRenderer_->locToCharPos(text,
                                       x,
                                       y,
                                       member->textFont(),
                                       member->textFontSize(),
                                       member->textFontStyle(),
                                       member->textFixedLineSpace(),
                                       member->textAlignment(),
                                       std::max(1, fieldWidth));
}

int CastLibManager::textLineHeight(int castLibNumber, int memberNumber) {
    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member || !member->isTextLike() || !textRenderer_) {
        return 0;
    }
    return textRenderer_->getLineHeight(member->textFont(),
                                        member->textFontSize(),
                                        member->textFontStyle(),
                                        member->textFixedLineSpace());
}

void CastLibManager::setFieldValue(const lingo::Datum& identifier, int castLibNumber, const std::string& value) {
    auto member = resolveFieldMember(identifier, castLibNumber);
    if (member) {
        member->setDynamicText(value);
    }
}

std::shared_ptr<chunks::CastMemberChunk> CastLibManager::getCastMember(int castLibNumber, int memberNumber) {
    auto castLib = getCastLib(castLibNumber);
    return castLib ? castLib->findMemberByNumber(memberNumber) : nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastLibManager::getCastMemberByName(const std::string& memberName) {
    ensureInitialized();
    for (const auto& [_, castLib] : castLibs_) {
        auto member = getCastLib(castLib->number())->findMemberByName(memberName);
        if (member) {
            return member;
        }
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::resolveMember(int castLibNumber, int memberNumber) {
    auto castLib = getCastLib(castLibNumber);
    return castLib ? castLib->getMember(memberNumber) : nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::findCastMemberByName(const std::string& memberName) {
    ensureInitialized();
    for (const auto& [_, castLib] : castLibs_) {
        if (castLib->isLoaded()) {
            if (auto dynamic = castLib->getMemberByName(memberName)) {
                return dynamic;
            }
        }
    }

    for (const auto& [_, castLib] : castLibs_) {
        auto loadedCast = getCastLib(castLib->number());
        if (auto chunk = loadedCast->findMemberByName(memberName)) {
            const int memberNumber = loadedCast->getMemberNumber(chunk);
            if (auto member = loadedCast->getMember(memberNumber)) {
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
    for (const auto& [_, castLib] : castLibs_) {
        if (!castLib->isLoaded()) {
            castLib->load();
        }
        const int memberNumber = castLib->getMemberNumber(target);
        if (memberNumber >= 0) {
            return castLib->getMember(memberNumber);
        }
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> CastLibManager::resolvePaletteByMember(int castLibNumber, int memberNumber) {
    if (memberNumber < 0) {
        return nullptr;
    }

    if (auto castLib = getCastLib(castLibNumber)) {
        if (auto palette = paletteFromCastMember(castLib, memberNumber)) {
            return palette;
        }
    }

    ensureInitialized();
    for (const auto& [number, castLib] : castLibs_) {
        if (number == castLibNumber) {
            continue;
        }
        if (auto palette = paletteFromCastMember(castLib, memberNumber)) {
            return palette;
        }
    }

    return file_ != nullptr ? file_->resolvePaletteByMemberNumber(memberNumber) : nullptr;
}

std::shared_ptr<const bitmap::Palette> CastLibManager::resolvePaletteById(int castLibNumber, int paletteId) {
    if (paletteId < 0) {
        return borrowedPalette(&bitmap::Palette::builtIn(paletteId));
    }

    ensureInitialized();
    if (castLibNumber > 0) {
        if (auto palette = paletteFromCastById(getCastLib(castLibNumber), paletteId)) {
            return palette;
        }
    }

    if (file_ != nullptr) {
        if (auto palette = file_->resolvePaletteExact(paletteId)) {
            return palette;
        }
    }

    for (const auto& [number, castLib] : castLibs_) {
        if (castLibNumber > 0 && number == castLibNumber) {
            continue;
        }
        if (!castLib || !castLib->isLoaded()) {
            continue;
        }
        if (auto palette = paletteFromCastById(castLib, paletteId)) {
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
    for (const auto& [_, castLib] : castLibs_) {
        if (!castLib) {
            continue;
        }
        if (!castLib->isLoaded()) {
            castLib->load();
        }
        auto member = castLib->getMemberByName(name);
        if (!member || !member->isPalette()) {
            continue;
        }
        if (auto palette = resolvePaletteByMember(castLib->number(), member->memberNum())) {
            return palette;
        }
    }
    return nullptr;
}

int CastLibManager::getScriptChunkId(int castLibNumber, int memberNumber) {
    auto castLib = getCastLib(castLibNumber);
    if (!castLib || !castLib->isLoaded()) {
        return -1;
    }
    auto script = castLib->getScript(memberNumber);
    return script ? script->id().value() : -1;
}

std::vector<std::string> CastLibManager::getScriptPropertyNames(int castLibNumber, int memberNumber) {
    auto castLib = getCastLib(castLibNumber);
    if (!castLib || !castLib->isLoaded()) {
        return {};
    }
    auto script = castLib->getScript(memberNumber);
    return script ? script->getPropertyNames() : std::vector<std::string>{};
}

lingo::Datum CastLibManager::createMember(int castLibNumber, const std::string& memberType) {
    auto castLib = getCastLib(castLibNumber);
    if (!castLib) {
        return lingo::Datum::voidValue();
    }
    auto member = castLib->createDynamicMember(memberType);
    if (!member) {
        return lingo::Datum::voidValue();
    }
    return lingo::Datum::castMemberRef(id::CastLibId(castLibNumber), id::MemberId(member->memberNum()));
}

lingo::Datum CastLibManager::createMember(const std::string& memberName, const std::string& memberType) {
    auto castLib = getCastLib(1);
    if (!castLib) {
        return lingo::Datum::voidValue();
    }
    auto member = castLib->createDynamicMember(memberName, memberType);
    if (!member) {
        return lingo::Datum::voidValue();
    }
    return lingo::Datum::of((castLib->number() << 16) | member->memberNum());
}

bool CastLibManager::eraseMember(int castLibNumber, int memberNumber) {
    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member) {
        return false;
    }
    const bool retiredDynamicSlot = member->isRuntimeDynamic();
    member->erase();
    if (retiredDynamicSlot && memberSlotRetiredCallback_) {
        memberSlotRetiredCallback_(castLibNumber, memberNumber);
    }
    return true;
}

lingo::Datum CastLibManager::callMemberMethod(int castLibNumber,
                                              int memberNumber,
                                              const std::string& methodName,
                                              const std::vector<lingo::Datum>& args) {
    if (equalsIgnoreCase(methodName, "erase")) {
        return eraseMember(castLibNumber, memberNumber) ? lingo::Datum::of(1) : lingo::Datum::voidValue();
    }

    auto member = resolveMember(castLibNumber, memberNumber);
    if (!member) {
        return lingo::Datum::voidValue();
    }

    if (equalsIgnoreCase(methodName, "duplicate") && !args.empty()) {
        const auto targetRef = duplicateTargetRefFromArg(args.front(), castLibNumber);
        if (targetRef.has_value()) {
            const int targetCastLibNumber = targetRef->castLib > 0 ? targetRef->castLib : castLibNumber;
            auto targetMember = resolveMember(targetCastLibNumber, targetRef->memberNum());
            if (!targetMember) {
                return lingo::Datum::voidValue();
            }
            if (member->isPalette() && targetMember->isPalette()) {
                auto receiverPalette = member->paletteData();
                if (!receiverPalette && !member->isRuntimeDynamic()) {
                    receiverPalette = resolvePaletteByMember(castLibNumber, memberNumber);
                }
                if (receiverPalette) {
                    targetMember->setPaletteData(receiverPalette);
                    return args.front();
                }
                auto argumentPalette = targetMember->paletteData();
                if (!argumentPalette && !targetMember->isRuntimeDynamic()) {
                    argumentPalette = resolvePaletteByMember(targetCastLibNumber, targetRef->memberNum());
                }
                if (argumentPalette) {
                    member->setPaletteData(argumentPalette);
                    return lingo::Datum::castMemberRef(id::CastLibId(castLibNumber), id::MemberId(memberNumber));
                }
            }
        }
    }

    if (equalsIgnoreCase(methodName, "getProp")) {
        if (args.size() < 2) {
            return lingo::Datum::voidValue();
        }
        const std::string propName = keyNameLikeJava(args[0]);
        const int index = args[1].intValue();
        const lingo::Datum propValue = getMemberProp(castLibNumber, memberNumber, propName);
        if (const auto* point = propValue.asIntPoint()) {
            return lingo::Datum::of(index == 1 ? point->x : point->y);
        }
        if (const auto* rect = propValue.asIntRect()) {
            switch (index) {
                case 1: return lingo::Datum::of(rect->left);
                case 2: return lingo::Datum::of(rect->top);
                case 3: return lingo::Datum::of(rect->right);
                case 4: return lingo::Datum::of(rect->bottom);
                default: return lingo::Datum::voidValue();
            }
        }
        if (propValue.isList()) {
            const auto& items = propValue.listValue().items();
            const int zeroIndex = index - 1;
            if (zeroIndex >= 0 && zeroIndex < static_cast<int>(items.size())) {
                return items[static_cast<std::size_t>(zeroIndex)];
            }
            return lingo::Datum::voidValue();
        }
        return propValue;
    }

    if (equalsIgnoreCase(methodName, "count")) {
        if (args.empty() || !member->isTextLike()) {
            return lingo::Datum::of(0);
        }
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        return lingo::Datum::of(countMethodTextChunks(text, keyNameLikeJava(args.front())));
    }

    if (!member->isTextLike()) {
        return lingo::Datum::voidValue();
    }

    if (equalsIgnoreCase(methodName, "charPosToLoc") || equalsIgnoreCase(methodName, "charposttoloc")) {
        if (args.empty() || !textRenderer_) {
            return lingo::Datum::intPoint(0, 0);
        }
        const int charIndex = args.front().intValue();
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        if (text.empty() || charIndex <= 0) {
            return lingo::Datum::intPoint(0, 0);
        }
        const int fieldWidth = std::max(1, member->textRectRight() - member->textRectLeft());
        const auto loc = charPosToLoc(member->castLib(), member->memberNum(), charIndex, fieldWidth);
        if (loc.size() < 2) {
            return lingo::Datum::intPoint(0, 0);
        }
        return lingo::Datum::intPoint(loc[0], loc[1]);
    }

    if (equalsIgnoreCase(methodName, "locToCharPos")) {
        if (args.empty() || !textRenderer_) {
            return lingo::Datum::of(0);
        }
        const auto* point = args.front().asIntPoint();
        const int x = point ? point->x : args.front().intValue();
        const int y = point ? point->y : (args.size() > 1 ? args[1].intValue() : 0);
        const std::string text = getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
        if (text.empty()) {
            return lingo::Datum::of(0);
        }
        const int fieldWidth = std::max(1, member->textRectRight() - member->textRectLeft());
        return lingo::Datum::of(locToCharPos(member->castLib(), member->memberNum(), x, y, fieldWidth));
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::importFileIntoMember(int castLibNumber,
                                          int memberNumber,
                                          const std::string& url,
                                          const lingo::Datum& options) {
    (void)options;
    auto castLib = getCastLib(castLibNumber);
    if (!castLib || castLib->getMember(memberNumber) == nullptr) {
        return false;
    }

    auto data = getCachedDownloadedData(url);
    if (!data.has_value() || data->empty()) {
        return false;
    }

    auto bitmap = decodeBitmapMedia(*data, castLibNumber, this);
    if (!bitmap.has_value()) {
        return false;
    }

    auto image = std::make_shared<bitmap::Bitmap>(std::move(*bitmap));
    return castLib->setMemberProp(memberNumber, "image", lingo::Datum::imageRef(std::move(image)));
}

void CastLibManager::cacheExternalData(const std::string& url, const std::vector<std::uint8_t>& data) {
    for (const auto& key : downloadCacheKeys(url)) {
        castDataCache_[key] = data;
    }
    for (const auto& castLib : findCastLibsByUrl(url)) {
        castLib->cacheFetchedExternalData(data);
    }
}

std::optional<std::vector<std::uint8_t>> CastLibManager::getCachedExternalData(const std::string& baseName) const {
    const auto found = castDataCache_.find(lower(baseName));
    if (found == castDataCache_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::vector<std::uint8_t>> CastLibManager::getCachedDownloadedData(const std::string& url) const {
    for (const auto& key : downloadCacheKeys(url)) {
        if (const auto found = castDataCache_.find(key); found != castDataCache_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

bool CastLibManager::setExternalCastData(int castLibNumber, const std::vector<std::uint8_t>& data) {
    ensureInitialized();
    const auto found = castLibs_.find(castLibNumber);
    if (found == castLibs_.end() || !found->second) {
        return false;
    }
    const bool loaded = found->second->setExternalData(data);
    if (loaded) {
        clearPendingExternalLoad(castLibNumber);
    }
    return loaded;
}

bool CastLibManager::setExternalCastDataByUrl(const std::string& url, const std::vector<std::uint8_t>& data) {
    ensureInitialized();
    bool anyLoaded = false;
    for (const auto& castLib : findCastLibsByUrl(url)) {
        if (castLib->isLoaded() && castLib->memberCount() > 0) {
            anyLoaded = true;
            continue;
        }
        if (setExternalCastData(castLib->number(), data)) {
            anyLoaded = true;
        }
    }
    return anyLoaded;
}

std::vector<int> CastLibManager::getMatchingCastLibNumbersByUrl(const std::string& url) {
    ensureInitialized();
    const auto baseName = util::getFileNameWithoutExtension(util::getFileName(url));
    std::vector<int> result;
    if (baseName.empty()) {
        return result;
    }
    for (const auto& [_, castLib] : castLibs_) {
        if (castLib->matchesAuthoredExternalFile(baseName)) {
            result.push_back(castLib->number());
        }
    }
    return result;
}

std::vector<int> CastLibManager::getRequestedExternalCastSlots(const std::string& url) {
    ensureInitialized();
    const auto baseName = util::getFileNameWithoutExtension(util::getFileName(url));
    std::vector<int> result;
    if (baseName.empty()) {
        return result;
    }

    for (const auto& castLib : findCastLibsByUrl(url)) {
        const int number = castLib->number();
        const auto pending = pendingExternalLoads_.find(number);
        const bool requested = castLib->isFetching() ||
                               (pending != pendingExternalLoads_.end() && equalsIgnoreCase(baseName, pending->second)) ||
                               (!castLib->isLoaded() && castLib->matchesAuthoredExternalFile(baseName));
        if (requested) {
            result.push_back(number);
        }
    }
    return result;
}

void CastLibManager::clearPendingExternalLoad(int castLibNumber) {
    pendingExternalLoads_.erase(castLibNumber);
}

void CastLibManager::setMemberSlotRetiredCallback(MemberSlotRetiredCallback callback) {
    memberSlotRetiredCallback_ = std::move(callback);
}

void CastLibManager::setTextRenderer(render::output::TextRenderer* renderer) {
    textRenderer_ = renderer;
}

void CastLibManager::installBuiltinCallbacks(lingo::builtin::BuiltinContext& context) {
    context.castMemberCreator = [this](int castLib, const std::string& memberType) {
        return createMember(castLib, memberType);
    };
    context.namedCastMemberCreator = [this](const std::string& memberName, const std::string& memberType) {
        return createMember(memberName, memberType);
    };
    context.castLibNumberResolver = [this](int castLib) {
        return getCastLibByNumber(castLib);
    };
    context.castLibNameResolver = [this](const std::string& name) {
        return getCastLibByName(name);
    };
    context.castLibCountSupplier = [this]() {
        return getCastLibCount();
    };
    context.castLibPropertyGetter = [this](int castLib, const std::string& propertyName) {
        return getCastLibProp(castLib, propertyName);
    };
    context.castLibPropertySetter = [this](int castLib, const std::string& propertyName, const lingo::Datum& value) {
        return setCastLibProp(castLib, propertyName, value);
    };
    context.castMemberCountSupplier = [this](int castLib) {
        return getMemberCount(castLib);
    };
    context.castMemberResolver = [this](int castLib, int memberNum) {
        return getMember(castLib, memberNum);
    };
    context.castMemberNameResolver = [this](int castLib, const std::string& memberName) {
        return getMemberByName(castLib, memberName);
    };
    context.registryCastMemberNameResolver = [this](int castLib, const std::string& memberName) {
        return getRegistryMemberByName(castLib, memberName);
    };
    context.castMemberExistsResolver = [this](int castLib, int memberNum) {
        return memberExists(castLib, memberNum);
    };
    context.registryVisibleMemberResolver = [this](int castLib, int memberNum) {
        return isRegistryVisibleMember(castLib, memberNum);
    };
    context.castMemberMethodHandler = [this](int castLib,
                                             int memberNum,
                                             const std::string& methodName,
                                             const std::vector<lingo::Datum>& args) {
        return callMemberMethod(castLib, memberNum, methodName, args);
    };
    context.castMemberPropertyGetter = [this](int castLib, int memberNum, const std::string& propertyName) {
        return getMemberProp(castLib, memberNum, propertyName);
    };
    context.castMemberPropertySetter = [this](int castLib, int memberNum, const std::string& propertyName, const lingo::Datum& value) {
        return setMemberProp(castLib, memberNum, propertyName, value);
    };
    context.fieldResolver = [this](const lingo::Datum& identifier, int castLib) {
        return getFieldDatum(identifier, castLib);
    };
    context.fieldParsedValueResolver = [this](int castLib, int memberNum, std::uint64_t revision) {
        return getParsedFieldValue(castLib, memberNum, revision);
    };
    context.fieldSetter = [this](const lingo::Datum& identifier, int castLib, const std::string& value) {
        setFieldValue(identifier, castLib, value);
    };
    context.scriptChunkIdResolver = [this](int castLib, int memberNum) {
        return getScriptChunkId(castLib, memberNum);
    };
    context.scriptPropertyNamesResolver = [this](int castLib, int memberNum) {
        return getScriptPropertyNames(castLib, memberNum);
    };
    context.importFileIntoHandler = [this](const lingo::Datum::CastMemberRef& ref,
                                           const std::string& url,
                                           const lingo::Datum& options) {
        return importFileIntoMember(ref.castLib > 0 ? ref.castLib : 1, ref.memberNum(), url, options);
    };
}

void CastLibManager::ensureInitialized() {
    if (initialized_ || !file_) {
        return;
    }
    initialized_ = true;

    auto castList = file_->castList();
    const auto& casts = file_->casts();
    const auto basePath = file_->basePath();
    (void)basePath;

    if (castList && !castList->entries().empty()) {
        for (int index = 0; index < static_cast<int>(castList->entries().size()); ++index) {
            const int castLibNumber = index + 1;
            const auto& listEntry = castList->entries()[static_cast<std::size_t>(index)];
            const bool isExternal = !listEntry.path.empty();
            std::shared_ptr<chunks::CastChunk> castChunk;
            if (!isExternal) {
                castChunk = file_->getMappedCastChunk(castLibNumber);
            } else if (index < static_cast<int>(casts.size())) {
                castChunk = casts[static_cast<std::size_t>(index)];
            }

            auto castLib = std::make_shared<CastLib>(castLibNumber, castChunk, &listEntry);
            castLib->setPreloadMode(listEntry.preloadSettings);
            if (!isExternal) {
                castLib->setSourceFile(file_);
            }
            castLibs_[castLibNumber] = castLib;
        }
    } else if (!casts.empty()) {
        for (int index = 0; index < static_cast<int>(casts.size()); ++index) {
            const int castLibNumber = index + 1;
            auto castLib = std::make_shared<CastLib>(castLibNumber, casts[static_cast<std::size_t>(index)], nullptr);
            castLib->setSourceFile(file_);
            castLibs_[castLibNumber] = castLib;
        }
    }
}

void CastLibManager::tryLoadCastFromCache(int castLibNumber, const std::string& newFileName) {
    if (newFileName.empty()) {
        return;
    }
    markPendingExternalLoad(castLibNumber, newFileName);
    if (castDataRequestCallback_) {
        castDataRequestCallback_(castLibNumber, newFileName);
    }
}

void CastLibManager::markPendingExternalLoad(int castLibNumber, const std::string& fileName) {
    pendingExternalLoads_[castLibNumber] = util::getFileNameWithoutExtension(util::getFileName(fileName));
}

std::vector<std::shared_ptr<CastLib>> CastLibManager::findCastLibsByUrl(const std::string& url) {
    ensureInitialized();
    const auto extractedFileName = util::getFileName(url);
    const auto fileNameNoExt = util::getFileNameWithoutExtension(extractedFileName);
    std::vector<std::shared_ptr<CastLib>> result;
    if (fileNameNoExt.empty()) {
        return result;
    }

    for (const auto& [_, castLib] : castLibs_) {
        const auto castPath = castLib->fileName();
        if (castPath.empty()) {
            continue;
        }
        const auto castFileNoExt = util::getFileNameWithoutExtension(util::getFileName(castPath));
        if (equalsIgnoreCase(castFileNoExt, fileNameNoExt)) {
            result.push_back(castLib);
        }
    }
    return result;
}

std::vector<std::string> CastLibManager::downloadCacheKeys(const std::string& url) {
    std::vector<std::string> keys;
    if (url.empty()) {
        return keys;
    }
    const auto fileName = util::getFileName(url);
    if (fileName.empty()) {
        return keys;
    }
    keys.push_back(lower(fileName));
    const auto baseName = util::getFileNameWithoutExtension(fileName);
    if (!baseName.empty()) {
        const auto loweredBase = lower(baseName);
        if (std::find(keys.begin(), keys.end(), loweredBase) == keys.end()) {
            keys.push_back(loweredBase);
        }
    }
    return keys;
}

bool CastLibManager::copyMemberMedia(int targetCastLibNumber,
                                     int targetMemberNumber,
                                     const lingo::Datum::CastMemberRef& sourceRef) {
    auto target = resolveMember(targetCastLibNumber, targetMemberNumber);
    const int sourceCastLib = sourceRef.castLib > 0 ? sourceRef.castLib : targetCastLibNumber;
    auto source = resolveMember(sourceCastLib, sourceRef.memberNum());
    if (!target || !source) {
        return false;
    }

    if (target->isTextLike() && source->isTextLike()) {
        const auto sourceCast = getCastLib(sourceCastLib);
        const auto sourceText = getMemberProp(source->castLib(), source->memberNum(), "text").stringValue();
        target->setDynamicText(sourceText);
        if (const auto styledText = authoredXmedText(sourceCast, source)) {
            applyTextStyleFromXmed(*target, *styledText);
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
        target->setEditable(source->editable());
        return true;
    }

    if (target->isBitmap() && source->isBitmap()) {
        if (auto bitmap = source->runtimeBitmap()) {
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
        auto palette = resolvePaletteByMember(sourceCastLib, source->memberNum());
        if (!palette) {
            return false;
        }
        target->setPaletteData(clonePalette(*palette));
        return true;
    }
    if (target->isScript() && source->isScript()) {
        auto sourceCastLibObject = getCastLib(sourceCastLib);
        auto script = sourceCastLibObject ? sourceCastLibObject->getScript(source->memberNum()) : nullptr;
        if (!script) {
            return false;
        }
        target->setRuntimeScript(std::move(script));
        return true;
    }
    return false;
}

lingo::Datum CastLibManager::getMemberByNameInCast(const std::shared_ptr<CastLib>& castLib,
                                                   const std::string& memberName) {
    if (!castLib || memberName.empty()) {
        return lingo::Datum::voidValue();
    }
    auto findByName = [&](const std::string& name) -> lingo::Datum {
        if (auto member = castLib->findMemberByName(name)) {
            const int memberNumber = castLib->getMemberNumber(member);
            if (memberNumber >= 0) {
                return lingo::Datum::castMemberRef(id::CastLibId(castLib->number()), id::MemberId(memberNumber));
            }
        }
        if (auto member = castLib->getMemberByName(name)) {
            return lingo::Datum::castMemberRef(id::CastLibId(castLib->number()), id::MemberId(member->memberNum()));
        }
        return lingo::Datum::voidValue();
    };
    auto found = findByName(memberName);
    if (!found.isVoid()) {
        return found;
    }
    if (const auto normalized = normalizeUnderscoreNumericTokens(memberName)) {
        return findByName(*normalized);
    }
    return lingo::Datum::voidValue();
}

bool CastLibManager::isRegistryFallbackEligibleCast(const std::shared_ptr<CastLib>& castLib) {
    return castLib != nullptr && castLib->usesStableRegistryBinding();
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLibManager::resolveFieldMember(
    const lingo::Datum& identifier,
    int castLibNumber) {
    ensureInitialized();

    if (identifier.isInt()) {
        const int rawNumber = identifier.intValue();
        int effectiveCastLib = castLibNumber > 0 ? castLibNumber : 1;
        int effectiveMemberNumber = rawNumber;
        if (rawNumber > 0xFFFF) {
            effectiveCastLib = (rawNumber >> 16) & 0xFFFF;
            effectiveMemberNumber = rawNumber & 0xFFFF;
        }
        return resolveMember(effectiveCastLib, effectiveMemberNumber);
    }

    const std::string memberName = identifier.stringValue();
    if (memberName.empty()) {
        return nullptr;
    }

    if (castLibNumber > 0) {
        auto castLib = getCastLib(castLibNumber);
        return castLib ? castLib->getMemberByName(memberName) : nullptr;
    }

    for (const auto& [_, castLib] : castLibs_) {
        auto loadedCast = getCastLib(castLib->number());
        if (!loadedCast) {
            continue;
        }
        if (auto member = loadedCast->getMemberByName(memberName)) {
            return member;
        }
    }
    return nullptr;
}

} // namespace libreshockwave::player::cast
