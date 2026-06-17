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

void seedDefaultEmbeddedFontsLocked(RegistryState& registry) {
    const auto& regular = ::libreshockwave::fonts::volter::regularData();
    if (regular.empty()) {
        return;
    }
    const auto& bold = ::libreshockwave::fonts::volter::boldData();
    const auto key = lowerAscii("Volter");
    registry.embeddedTtfFonts[key][0] = EmbeddedTtfVariants{regular, bold, {}, {}};
    registry.canonicalIndex[FontRegistry::canonicalFontName("Volter")] = key;
    if (!registry.firstEmbeddedFont.has_value()) {
        registry.firstEmbeddedFont = "Volter";
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
                                                              bool italic) {
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
    if (!bold && !italic) {
        if (const auto ttf = registry.ttfCache.find(key); ttf != registry.ttfCache.end()) {
            auto rasterized = font::TtfBitmapRasterizer::rasterize(ttf->second, fontSize, fontName);
            if (rasterized != nullptr) {
                registry.rasterizedCache[fontCacheKey(fontName, fontSize, bold, italic)] = rasterized;
                return rasterized;
            }
        }
    }
    if (const auto parsed = registry.parsedFonts.find(key); parsed != registry.parsedFonts.end()) {
        auto rasterized = font::BitmapFont::fromPfr1(*parsed->second, fontSize);
        if (rasterized != nullptr) {
            registry.rasterizedCache[fontCacheKey(fontName, fontSize, bold, italic)] = rasterized;
            return rasterized;
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
                registry.rasterizedCache[cacheKey] = rasterized;
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
                                                                      bool italic) {
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
        registry.rasterizedCache[cacheKey] = rasterized;
    }
    return rasterized;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getPfrBitmapFont(const std::string& fontName,
                                                                 int fontSize) {
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
    if (rasterized != nullptr) {
        registry.rasterizedCache[cacheKey] = rasterized;
    }
    return rasterized;
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
    registry.embeddedTtfFonts.clear();
    registry.canonicalIndex.clear();
    registry.aliases.clear();
    registry.firstRegisteredFont.reset();
    registry.firstEmbeddedFont.reset();
    seedDefaultEmbeddedFontsLocked(registry);
}

} // namespace libreshockwave::player::cast
