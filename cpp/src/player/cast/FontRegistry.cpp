#include "libreshockwave/player/cast/FontRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "libreshockwave/font/Pfr1TtfConverter.hpp"
#include "libreshockwave/font/TtfBitmapRasterizer.hpp"
#include "libreshockwave/fonts/volter/Volter.hpp"
#include "libreshockwave/player/cast/MacFontBundle.hpp"
#include "libreshockwave/player/cast/WindowsFontBundle.hpp"

namespace libreshockwave::player::cast {
namespace {

struct EmbeddedTtfVariants {
    std::vector<std::uint8_t> regular;
    std::vector<std::uint8_t> bold;
    std::vector<std::uint8_t> italic;
    std::vector<std::uint8_t> boldItalic;

    [[nodiscard]] const std::vector<std::uint8_t>* get(bool boldRequested, bool italicRequested) const {
        if (boldRequested && italicRequested && !boldItalic.empty()) {
            return &boldItalic;
        }
        if (boldRequested && !bold.empty()) {
            return &bold;
        }
        if (italicRequested && !italic.empty()) {
            return &italic;
        }
        return regular.empty() ? nullptr : &regular;
    }

    [[nodiscard]] bool hasBold() const {
        return !bold.empty();
    }
};

struct EmbeddedTtfSelection {
    int fontSize = 0;
    const EmbeddedTtfVariants* variants = nullptr;
};

struct RegistryState {
    std::unordered_map<std::string, std::shared_ptr<font::Pfr1Font>> parsedFonts;
    std::unordered_map<std::string, std::vector<std::uint8_t>> ttfCache;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> rasterizedCache;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> fallbackSelectionCache;
    std::unordered_map<std::string, std::map<int, EmbeddedTtfVariants>> embeddedTtfFonts;
    std::unordered_map<std::string, std::string> canonicalIndex;
    std::unordered_map<std::string, FontRegistry::FontAlias> aliases;
    std::optional<std::string> firstRegisteredFont;
    std::optional<std::string> firstEmbeddedFont;
    std::mutex mutex;
};

RegistryState& stateRaw() {
    static RegistryState registry;
    return registry;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trimAscii(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    if (first > 0) {
        value.erase(0, first);
    }
    return value;
}

bool isBlank(const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

bool hasTrailingDigitSegment(std::string_view value) {
    if (value.empty() || !std::isdigit(static_cast<unsigned char>(value.back()))) {
        return false;
    }
    std::size_t pos = value.size();
    while (pos > 0 && std::isdigit(static_cast<unsigned char>(value[pos - 1])) != 0) {
        --pos;
    }
    return pos > 0 && std::isspace(static_cast<unsigned char>(value[pos - 1])) != 0;
}

std::string stripTrailingDigitSegments(std::string value) {
    while (hasTrailingDigitSegment(value)) {
        while (!value.empty() && std::isdigit(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
    }
    return value;
}

std::string fontCacheKey(const std::string& fontName, int fontSize, bool bold, bool italic) {
    auto key = lowerAscii(fontName) + ":" + std::to_string(fontSize);
    if (bold || italic) {
        key += ":" + std::string(bold ? "1" : "0") + ":" + (italic ? "1" : "0");
    }
    return key;
}

std::string embeddedFontCacheKey(const std::string& fontName, int fontSize, bool bold, bool italic) {
    return lowerAscii(fontName) + ":" + std::to_string(fontSize) + ":embedded:" +
           std::string(bold ? "1" : "0") + ":" + (italic ? "1" : "0");
}

int readU16(const std::vector<std::uint8_t>& data, int offset) {
    if (offset < 0 || offset + 2 > static_cast<int>(data.size())) {
        return 0;
    }
    return ((data[static_cast<std::size_t>(offset)] & 0xFF) << 8) |
           (data[static_cast<std::size_t>(offset + 1)] & 0xFF);
}

int readI32(const std::vector<std::uint8_t>& data, int offset) {
    if (offset < 0 || offset + 4 > static_cast<int>(data.size())) {
        return 0;
    }
    const std::uint32_t value =
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset)] & 0xFF) << 24U) |
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 1)] & 0xFF) << 16U) |
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 2)] & 0xFF) << 8U) |
        static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 3)] & 0xFF);
    return static_cast<int>(static_cast<std::int32_t>(value));
}

std::string decodeTtfNameString(const std::vector<std::uint8_t>& data,
                                int offset,
                                int length,
                                int platformId) {
    if (offset < 0 || length <= 0 || offset + length > static_cast<int>(data.size())) {
        return {};
    }
    std::string result;
    if (platformId == 0 || platformId == 3) {
        for (int pos = offset; pos + 1 < offset + length; pos += 2) {
            const int codePoint = readU16(data, pos);
            if (codePoint >= 0x20 && codePoint <= 0x7E) {
                result.push_back(static_cast<char>(codePoint));
            }
        }
    } else {
        for (int pos = offset; pos < offset + length; ++pos) {
            const auto ch = data[static_cast<std::size_t>(pos)];
            if (ch >= 0x20 && ch <= 0x7E) {
                result.push_back(static_cast<char>(ch));
            }
        }
    }
    return trimAscii(std::move(result));
}

std::string ttfFamilyName(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12) {
        return {};
    }
    const int tableCount = readU16(data, 4);
    int nameTableOffset = 0;
    int nameTableLength = 0;
    for (int index = 0; index < tableCount; ++index) {
        const int recordOffset = 12 + index * 16;
        if (recordOffset + 16 > static_cast<int>(data.size())) {
            break;
        }
        const std::string tag(reinterpret_cast<const char*>(data.data() + recordOffset), 4);
        if (tag == "name") {
            nameTableOffset = readI32(data, recordOffset + 8);
            nameTableLength = readI32(data, recordOffset + 12);
            break;
        }
    }
    if (nameTableOffset <= 0 || nameTableLength < 6 || nameTableOffset + 6 > static_cast<int>(data.size())) {
        return {};
    }
    const int count = readU16(data, nameTableOffset + 2);
    const int stringOffset = nameTableOffset + readU16(data, nameTableOffset + 4);
    std::string fallback;
    for (int index = 0; index < count; ++index) {
        const int recordOffset = nameTableOffset + 6 + index * 12;
        if (recordOffset + 12 > static_cast<int>(data.size())) {
            break;
        }
        const int platformId = readU16(data, recordOffset);
        const int languageId = readU16(data, recordOffset + 4);
        const int nameId = readU16(data, recordOffset + 6);
        const int length = readU16(data, recordOffset + 8);
        const int offset = stringOffset + readU16(data, recordOffset + 10);
        if (nameId != 1) {
            continue;
        }
        const auto decoded = decodeTtfNameString(data, offset, length, platformId);
        if (decoded.empty()) {
            continue;
        }
        if ((platformId == 3 && languageId == 0x0409) || platformId == 0) {
            return decoded;
        }
        if (fallback.empty()) {
            fallback = decoded;
        }
    }
    return fallback;
}

void seedDefaultEmbeddedFontsLocked(RegistryState& registry) {
    const auto& regular = ::libreshockwave::fonts::volter::regularData();
    if (regular.empty()) {
        return;
    }
    const auto& bold = ::libreshockwave::fonts::volter::boldData();
    auto fontName = ttfFamilyName(regular);
    if (fontName.empty()) {
        fontName = "Director Pixel";
    }
    const auto key = lowerAscii(fontName);
    registry.embeddedTtfFonts[key][0] = EmbeddedTtfVariants{regular, bold, {}, {}};
    registry.canonicalIndex[FontRegistry::canonicalFontName(fontName)] = key;
    if (!registry.firstEmbeddedFont.has_value()) {
        registry.firstEmbeddedFont = std::move(fontName);
    }
}

void ensureDefaultEmbeddedFonts() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = stateRaw();
        std::lock_guard lock(registry.mutex);
        seedDefaultEmbeddedFontsLocked(registry);
    });
}

RegistryState& state() {
    ensureDefaultEmbeddedFonts();
    return stateRaw();
}

std::optional<EmbeddedTtfSelection> selectEmbeddedTtf(
    const std::map<int, EmbeddedTtfVariants>& family,
    int requestedSize) {
    if (family.empty()) {
        return std::nullopt;
    }
    if (const auto exact = family.find(requestedSize); exact != family.end()) {
        return EmbeddedTtfSelection{requestedSize, &exact->second};
    }
    if (const auto scalable = family.find(0); scalable != family.end()) {
        return EmbeddedTtfSelection{requestedSize, &scalable->second};
    }

    auto best = family.end();
    int bestDistance = 0;
    for (auto it = family.begin(); it != family.end(); ++it) {
        if (it->first <= 0) {
            continue;
        }
        const int distance = it->first > requestedSize ? it->first - requestedSize : requestedSize - it->first;
        if (best == family.end() || distance < bestDistance) {
            best = it;
            bestDistance = distance;
        }
    }
    if (best == family.end()) {
        return std::nullopt;
    }
    return EmbeddedTtfSelection{best->first, &best->second};
}

} // namespace

void FontRegistry::registerBitmapFont(const std::string& fontName,
                                      int fontSize,
                                      std::shared_ptr<font::BitmapFont> font) {
    if (fontName.empty() || font == nullptr) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.rasterizedCache[fontCacheKey(fontName, fontSize, false, false)] = std::move(font);
}

void FontRegistry::registerPfr1Font(const std::string& memberName,
                                    const std::vector<std::uint8_t>& pfrData) {
    if (memberName.empty() || pfrData.empty()) {
        return;
    }
    auto parsed = font::Pfr1Font::parse(pfrData);
    if (parsed == nullptr) {
        return;
    }

    std::vector<std::uint8_t> ttfBytes;
    try {
        const auto& ttfName = parsed->fontName.empty() ? memberName : parsed->fontName;
        ttfBytes = font::Pfr1TtfConverter::convert(*parsed, ttfName);
    } catch (const std::exception&) {
        ttfBytes.clear();
    }

    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(memberName);
    registry.parsedFonts[key] = parsed;
    if (!ttfBytes.empty()) {
        registry.ttfCache[key] = ttfBytes;
    }
    if (!registry.firstRegisteredFont.has_value()) {
        registry.firstRegisteredFont = key;
    }
    registry.canonicalIndex[canonicalFontName(memberName)] = key;

    if (!parsed->fontName.empty()) {
        const auto internalKey = lowerAscii(parsed->fontName);
        registry.parsedFonts[internalKey] = parsed;
        if (!ttfBytes.empty()) {
            registry.ttfCache[internalKey] = ttfBytes;
        }
        registry.canonicalIndex[canonicalFontName(parsed->fontName)] = key;
    }
}

std::shared_ptr<font::Pfr1Font> FontRegistry::getPfr1Font(const std::string& fontName) {
    if (fontName.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);
    if (const auto it = registry.parsedFonts.find(key); it != registry.parsedFonts.end()) {
        return it->second;
    }
    if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
        mapped != registry.canonicalIndex.end()) {
        if (const auto it = registry.parsedFonts.find(mapped->second); it != registry.parsedFonts.end()) {
            return it->second;
        }
    }

    return nullptr;
}

std::optional<std::vector<std::uint8_t>> FontRegistry::getTtfBytes(const std::string& fontName) {
    if (fontName.empty()) {
        return std::nullopt;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.ttfCache.find(lowerAscii(fontName)); it != registry.ttfCache.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getBitmapFont(const std::string& fontName, int fontSize) {
    return getBitmapFont(fontName, fontSize, false, false);
}

std::shared_ptr<font::BitmapFont> FontRegistry::getBitmapFont(const std::string& fontName,
                                                              int fontSize,
                                                              bool bold,
                                                              bool italic,
                                                              bool cache) {
    if (fontName.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto styled = registry.rasterizedCache.find(fontCacheKey(fontName, fontSize, bold, italic));
        styled != registry.rasterizedCache.end()) {
        return styled->second;
    }
    const auto key = lowerAscii(fontName);
    if (const auto parsed = registry.parsedFonts.find(key); parsed != registry.parsedFonts.end()) {
        auto rasterized = font::BitmapFont::fromPfr1(*parsed->second, fontSize);
        if (rasterized != nullptr) {
            if (cache) {
                registry.rasterizedCache[fontCacheKey(fontName, fontSize, bold, italic)] = rasterized;
            }
            return rasterized;
        }
    }
    if (!bold && !italic) {
        if (const auto ttf = registry.ttfCache.find(key); ttf != registry.ttfCache.end()) {
            auto rasterized = font::TtfBitmapRasterizer::rasterize(ttf->second, fontSize, fontName);
            if (rasterized != nullptr) {
                if (cache) {
                    registry.rasterizedCache[fontCacheKey(fontName, fontSize, bold, italic)] = rasterized;
                }
                return rasterized;
            }
        }
    }

    const auto embeddedKey = registry.embeddedTtfFonts.contains(key)
        ? key
        : [&registry, &fontName]() -> std::string {
              if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
                  mapped != registry.canonicalIndex.end() && registry.embeddedTtfFonts.contains(mapped->second)) {
                  return mapped->second;
              }
              return {};
          }();
    if (!embeddedKey.empty()) {
        const auto selected = selectEmbeddedTtf(registry.embeddedTtfFonts.at(embeddedKey), fontSize);
        if (!selected.has_value() || selected->variants == nullptr) {
            return nullptr;
        }
        const auto cacheKey = embeddedFontCacheKey(embeddedKey, selected->fontSize, bold, italic);
        if (const auto cached = registry.rasterizedCache.find(cacheKey); cached != registry.rasterizedCache.end()) {
            return cached->second;
        }
        const auto* ttfBytes = selected->variants->get(bold, italic);
        if (ttfBytes != nullptr) {
            auto rasterized = font::TtfBitmapRasterizer::rasterize(*ttfBytes, selected->fontSize, fontName);
            if (rasterized != nullptr) {
                if (cache) {
                    registry.rasterizedCache[cacheKey] = rasterized;
                }
                return rasterized;
            }
        }
    }
    if (auto windowsFont = WindowsFontBundle::getFont(fontName, fontSize, bold, italic); windowsFont != nullptr) {
        return windowsFont;
    }
    if (auto macFont = MacFontBundle::getFont(fontName, fontSize, bold, italic); macFont != nullptr) {
        return macFont;
    }
    return nullptr;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getEmbeddedBitmapFont(const std::string& fontName,
                                                                      int fontSize,
                                                                      bool bold,
                                                                      bool italic,
                                                                      bool cache) {
    if (fontName.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);
    const auto embeddedKey = registry.embeddedTtfFonts.contains(key)
        ? key
        : [&registry, &fontName]() -> std::string {
              if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
                  mapped != registry.canonicalIndex.end() && registry.embeddedTtfFonts.contains(mapped->second)) {
                  return mapped->second;
              }
              return {};
          }();
    if (embeddedKey.empty()) {
        return nullptr;
    }

    const auto selected = selectEmbeddedTtf(registry.embeddedTtfFonts.at(embeddedKey), fontSize);
    if (!selected.has_value() || selected->variants == nullptr) {
        return nullptr;
    }
    const auto cacheKey = embeddedFontCacheKey(embeddedKey, selected->fontSize, bold, italic);
    if (const auto cached = registry.rasterizedCache.find(cacheKey); cached != registry.rasterizedCache.end()) {
        return cached->second;
    }
    const auto* ttfBytes = selected->variants->get(bold, italic);
    if (ttfBytes == nullptr) {
        return nullptr;
    }
    auto rasterized = font::TtfBitmapRasterizer::rasterize(*ttfBytes, selected->fontSize, fontName);
    if (rasterized != nullptr) {
        if (cache) {
            registry.rasterizedCache[cacheKey] = rasterized;
        }
    }
    return rasterized;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getPfrBitmapFont(const std::string& fontName,
                                                                 int fontSize,
                                                                 bool cache) {
    if (fontName.empty() || fontSize <= 0) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    auto key = lowerAscii(fontName);
    auto parsed = registry.parsedFonts.find(key);
    if (parsed == registry.parsedFonts.end()) {
        if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
            mapped != registry.canonicalIndex.end()) {
            key = mapped->second;
            parsed = registry.parsedFonts.find(key);
        }
    }
    if (parsed == registry.parsedFonts.end()) {
        return nullptr;
    }

    const auto cacheKey = fontCacheKey(key, fontSize, true, false);
    if (const auto cached = registry.rasterizedCache.find(cacheKey); cached != registry.rasterizedCache.end()) {
        return cached->second;
    }
    auto rasterized = font::BitmapFont::fromPfr1(*parsed->second, fontSize);
    if (rasterized != nullptr && cache) {
        registry.rasterizedCache[cacheKey] = rasterized;
    }
    return rasterized;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getFallbackSelection(const std::string& fontName,
                                                                     int fontSize,
                                                                     bool bold) {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto it = registry.fallbackSelectionCache.find(fontCacheKey(fontName, fontSize, bold, false));
    return it == registry.fallbackSelectionCache.end() ? nullptr : it->second;
}

void FontRegistry::setFallbackSelection(const std::string& fontName,
                                        int fontSize,
                                        bool bold,
                                        std::shared_ptr<font::BitmapFont> font) {
    if (fontName.empty() || font == nullptr) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.fallbackSelectionCache[fontCacheKey(fontName, fontSize, bold, false)] = std::move(font);
}

void FontRegistry::registerEmbeddedTtfFont(const std::string& fontName,
                                           std::vector<std::uint8_t> regular,
                                           std::vector<std::uint8_t> bold,
                                           std::vector<std::uint8_t> italic,
                                           std::vector<std::uint8_t> boldItalic) {
    registerEmbeddedTtfFont(fontName,
                            0,
                            std::move(regular),
                            std::move(bold),
                            std::move(italic),
                            std::move(boldItalic));
}

void FontRegistry::registerEmbeddedTtfFont(const std::string& fontName,
                                           int fontSize,
                                           std::vector<std::uint8_t> regular,
                                           std::vector<std::uint8_t> bold,
                                           std::vector<std::uint8_t> italic,
                                           std::vector<std::uint8_t> boldItalic) {
    if (fontName.empty() || isBlank(fontName) || fontSize < 0 || regular.empty()) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);
    registry.embeddedTtfFonts[key][fontSize] = EmbeddedTtfVariants{std::move(regular),
                                                                    std::move(bold),
                                                                    std::move(italic),
                                                                    std::move(boldItalic)};
    registry.canonicalIndex[canonicalFontName(fontName)] = key;
    if (!registry.firstEmbeddedFont.has_value()) {
        registry.firstEmbeddedFont = fontName;
    }
}

bool FontRegistry::hasEmbeddedBoldVariant(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);
    auto it = registry.embeddedTtfFonts.find(key);
    if (it == registry.embeddedTtfFonts.end()) {
        if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
            mapped != registry.canonicalIndex.end()) {
            it = registry.embeddedTtfFonts.find(mapped->second);
        }
    }
    return it != registry.embeddedTtfFonts.end() &&
           std::any_of(it->second.begin(), it->second.end(), [](const auto& entry) {
               return entry.second.hasBold();
           });
}

void FontRegistry::registerFontAlias(const std::string& alias,
                                     const std::string& fontName,
                                     bool bold) {
    if (alias.empty() || isBlank(alias) || fontName.empty() || isBlank(fontName)) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(alias);
    registry.aliases[key] = FontAlias{fontName, bold};
    registry.canonicalIndex[canonicalFontName(alias)] = key;
}

std::optional<FontRegistry::FontAlias> FontRegistry::getFontAlias(const std::string& fontName) {
    if (fontName.empty()) {
        return std::nullopt;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.aliases.find(lowerAscii(fontName)); it != registry.aliases.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string FontRegistry::canonicalFontName(const std::string& name) {
    if (name.empty()) {
        return "";
    }
    std::string result = lowerAscii(name);
    std::replace(result.begin(), result.end(), '_', ' ');
    std::replace(result.begin(), result.end(), '*', ' ');
    return trimAscii(stripTrailingDigitSegments(trimAscii(std::move(result))));
}

std::optional<std::string> FontRegistry::resolveFont(const std::string& fontName) {
    if (fontName.empty()) {
        return std::nullopt;
    }

    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);

    if (registry.parsedFonts.contains(key)) {
        return key;
    }

    if (const auto mapped = registry.canonicalIndex.find(canonicalFontName(fontName));
        mapped != registry.canonicalIndex.end()) {
        return mapped->second;
    }

    if (key.size() <= 3) {
        for (const auto& [fontKey, _] : registry.parsedFonts) {
            if (fontKey.starts_with(key)) {
                return fontKey;
            }
        }
    }

    return std::nullopt;
}

bool FontRegistry::hasPfrFont(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(fontName);
    return registry.parsedFonts.contains(key);
}

std::optional<std::string> FontRegistry::getFirstRegisteredFont() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    return registry.firstRegisteredFont;
}

std::optional<std::string> FontRegistry::getPreferredDirectorPixelFont() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (registry.firstEmbeddedFont.has_value() && !registry.firstEmbeddedFont->empty()) {
        return registry.firstEmbeddedFont;
    }
    return registry.firstRegisteredFont;
}

void FontRegistry::clear() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.parsedFonts.clear();
    registry.ttfCache.clear();
    registry.rasterizedCache.clear();
    registry.fallbackSelectionCache.clear();
    registry.embeddedTtfFonts.clear();
    registry.canonicalIndex.clear();
    registry.aliases.clear();
    registry.firstRegisteredFont.reset();
    registry.firstEmbeddedFont.reset();
    seedDefaultEmbeddedFontsLocked(registry);
}

} // namespace libreshockwave::player::cast
