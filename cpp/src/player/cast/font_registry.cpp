#include "libreshockwave/player/cast/font_registry.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "libreshockwave/font/pfr1_ttf_converter.hpp"
#include "libreshockwave/font/ttf_bitmap_rasterizer.hpp"
#include "libreshockwave/fonts/volter/volter.hpp"
#include "libreshockwave/player/cast/mac_font_bundle.hpp"
#include "libreshockwave/player/cast/windows_font_bundle.hpp"

namespace libreshockwave::player::cast {
namespace {

struct EmbeddedTtfVariants {
    std::vector<std::uint8_t> regular;
    std::vector<std::uint8_t> bold;
    std::vector<std::uint8_t> italic;
    std::vector<std::uint8_t> bold_italic;

    [[nodiscard]] const std::vector<std::uint8_t>* get(bool bold_requested, bool italic_requested) const {
        if (bold_requested && italic_requested && !bold_italic.empty()) {
            return &bold_italic;
        }
        if (bold_requested && !bold.empty()) {
            return &bold;
        }
        if (italic_requested && !italic.empty()) {
            return &italic;
        }
        return regular.empty() ? nullptr : &regular;
    }

    [[nodiscard]] bool hasBold() const {
        return !bold.empty();
    }
};

struct EmbeddedTtfSelection {
    int font_size = 0;
    const EmbeddedTtfVariants* variants = nullptr;
};

struct RegistryState {
    std::unordered_map<std::string, std::shared_ptr<font::Pfr1Font>> parsed_fonts;
    std::unordered_map<std::string, std::vector<std::uint8_t>> ttf_cache;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> rasterized_cache;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> fallback_selection_cache;
    std::unordered_map<std::string, std::map<int, EmbeddedTtfVariants>> embedded_ttf_fonts;
    std::unordered_map<std::string, std::string> canonical_index;
    std::unordered_map<std::string, FontRegistry::FontAlias> aliases;
    std::optional<std::string> first_registered_font;
    std::optional<std::string> first_embedded_font;
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

std::string fontCacheKey(const std::string& font_name, int font_size, bool bold, bool italic) {
    auto key = lowerAscii(font_name) + ":" + std::to_string(font_size);
    if (bold || italic) {
        key += ":" + std::string(bold ? "1" : "0") + ":" + (italic ? "1" : "0");
    }
    return key;
}

std::string embeddedFontCacheKey(const std::string& font_name, int font_size, bool bold, bool italic) {
    return lowerAscii(font_name) + ":" + std::to_string(font_size) + ":embedded:" +
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
                                int platform_id) {
    if (offset < 0 || length <= 0 || offset + length > static_cast<int>(data.size())) {
        return {};
    }
    std::string result;
    if (platform_id == 0 || platform_id == 3) {
        for (int pos = offset; pos + 1 < offset + length; pos += 2) {
            const int code_point = readU16(data, pos);
            if (code_point >= 0x20 && code_point <= 0x7E) {
                result.push_back(static_cast<char>(code_point));
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
    const int table_count = readU16(data, 4);
    int name_table_offset = 0;
    int name_table_length = 0;
    for (int index = 0; index < table_count; ++index) {
        const int record_offset = 12 + index * 16;
        if (record_offset + 16 > static_cast<int>(data.size())) {
            break;
        }
        const std::string tag(reinterpret_cast<const char*>(data.data() + record_offset), 4);
        if (tag == "name") {
            name_table_offset = readI32(data, record_offset + 8);
            name_table_length = readI32(data, record_offset + 12);
            break;
        }
    }
    if (name_table_offset <= 0 || name_table_length < 6 || name_table_offset + 6 > static_cast<int>(data.size())) {
        return {};
    }
    const int count = readU16(data, name_table_offset + 2);
    const int string_offset = name_table_offset + readU16(data, name_table_offset + 4);
    std::string fallback;
    for (int index = 0; index < count; ++index) {
        const int record_offset = name_table_offset + 6 + index * 12;
        if (record_offset + 12 > static_cast<int>(data.size())) {
            break;
        }
        const int platform_id = readU16(data, record_offset);
        const int language_id = readU16(data, record_offset + 4);
        const int name_id = readU16(data, record_offset + 6);
        const int length = readU16(data, record_offset + 8);
        const int offset = string_offset + readU16(data, record_offset + 10);
        if (name_id != 1) {
            continue;
        }
        const auto decoded = decodeTtfNameString(data, offset, length, platform_id);
        if (decoded.empty()) {
            continue;
        }
        if ((platform_id == 3 && language_id == 0x0409) || platform_id == 0) {
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
    auto font_name = ttfFamilyName(regular);
    if (font_name.empty()) {
        font_name = "Director Pixel";
    }
    const auto key = lowerAscii(font_name);
    registry.embedded_ttf_fonts[key][0] = EmbeddedTtfVariants{regular, bold, {}, {}};
    registry.canonical_index[FontRegistry::canonicalFontName(font_name)] = key;
    if (!registry.first_embedded_font.has_value()) {
        registry.first_embedded_font = std::move(font_name);
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
    int requested_size) {
    if (family.empty()) {
        return std::nullopt;
    }
    if (const auto exact = family.find(requested_size); exact != family.end()) {
        return EmbeddedTtfSelection{requested_size, &exact->second};
    }
    if (const auto scalable = family.find(0); scalable != family.end()) {
        return EmbeddedTtfSelection{requested_size, &scalable->second};
    }

    auto best = family.end();
    int best_distance = 0;
    for (auto it = family.begin(); it != family.end(); ++it) {
        if (it->first <= 0) {
            continue;
        }
        const int distance = it->first > requested_size ? it->first - requested_size : requested_size - it->first;
        if (best == family.end() || distance < best_distance) {
            best = it;
            best_distance = distance;
        }
    }
    if (best == family.end()) {
        return std::nullopt;
    }
    return EmbeddedTtfSelection{best->first, &best->second};
}

} // namespace

void FontRegistry::registerBitmapFont(const std::string& font_name,
                                      int font_size,
                                      std::shared_ptr<font::BitmapFont> font) {
    if (font_name.empty() || font == nullptr) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.rasterized_cache[fontCacheKey(font_name, font_size, false, false)] = std::move(font);
}

void FontRegistry::registerPfr1Font(const std::string& member_name,
                                    const std::vector<std::uint8_t>& pfr_data) {
    if (member_name.empty() || pfr_data.empty()) {
        return;
    }
    auto parsed = font::Pfr1Font::parse(pfr_data);
    if (parsed == nullptr) {
        return;
    }

    std::vector<std::uint8_t> ttf_bytes;
    try {
        const auto& ttf_name = parsed->font_name.empty() ? member_name : parsed->font_name;
        ttf_bytes = font::Pfr1TtfConverter::convert(*parsed, ttf_name);
    } catch (const std::exception&) {
        ttf_bytes.clear();
    }

    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(member_name);
    registry.parsed_fonts[key] = parsed;
    if (!ttf_bytes.empty()) {
        registry.ttf_cache[key] = ttf_bytes;
    }
    if (!registry.first_registered_font.has_value()) {
        registry.first_registered_font = key;
    }
    registry.canonical_index[canonicalFontName(member_name)] = key;

    if (!parsed->font_name.empty()) {
        const auto internal_key = lowerAscii(parsed->font_name);
        registry.parsed_fonts[internal_key] = parsed;
        if (!ttf_bytes.empty()) {
            registry.ttf_cache[internal_key] = ttf_bytes;
        }
        registry.canonical_index[canonicalFontName(parsed->font_name)] = key;
    }
}

std::shared_ptr<font::Pfr1Font> FontRegistry::getPfr1Font(const std::string& font_name) {
    if (font_name.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);
    if (const auto it = registry.parsed_fonts.find(key); it != registry.parsed_fonts.end()) {
        return it->second;
    }
    if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
        mapped != registry.canonical_index.end()) {
        if (const auto it = registry.parsed_fonts.find(mapped->second); it != registry.parsed_fonts.end()) {
            return it->second;
        }
    }

    return nullptr;
}

std::optional<std::vector<std::uint8_t>> FontRegistry::getTtfBytes(const std::string& font_name) {
    if (font_name.empty()) {
        return std::nullopt;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.ttf_cache.find(lowerAscii(font_name)); it != registry.ttf_cache.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getBitmapFont(const std::string& font_name, int font_size) {
    return getBitmapFont(font_name, font_size, false, false);
}

std::shared_ptr<font::BitmapFont> FontRegistry::getBitmapFont(const std::string& font_name,
                                                              int font_size,
                                                              bool bold,
                                                              bool italic,
                                                              bool cache) {
    if (font_name.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto styled = registry.rasterized_cache.find(fontCacheKey(font_name, font_size, bold, italic));
        styled != registry.rasterized_cache.end()) {
        return styled->second;
    }
    const auto key = lowerAscii(font_name);
    if (const auto parsed = registry.parsed_fonts.find(key); parsed != registry.parsed_fonts.end()) {
        auto rasterized = font::BitmapFont::fromPfr1(*parsed->second, font_size);
        if (rasterized != nullptr) {
            if (cache) {
                registry.rasterized_cache[fontCacheKey(font_name, font_size, bold, italic)] = rasterized;
            }
            return rasterized;
        }
    }
    if (!bold && !italic) {
        if (const auto ttf = registry.ttf_cache.find(key); ttf != registry.ttf_cache.end()) {
            auto rasterized = font::TtfBitmapRasterizer::rasterize(ttf->second, font_size, font_name);
            if (rasterized != nullptr) {
                if (cache) {
                    registry.rasterized_cache[fontCacheKey(font_name, font_size, bold, italic)] = rasterized;
                }
                return rasterized;
            }
        }
    }

    const auto embedded_key = registry.embedded_ttf_fonts.contains(key)
        ? key
        : [&registry, &font_name]() -> std::string {
              if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
                  mapped != registry.canonical_index.end() && registry.embedded_ttf_fonts.contains(mapped->second)) {
                  return mapped->second;
              }
              return {};
          }();
    if (!embedded_key.empty()) {
        const auto selected = selectEmbeddedTtf(registry.embedded_ttf_fonts.at(embedded_key), font_size);
        if (!selected.has_value() || selected->variants == nullptr) {
            return nullptr;
        }
        const auto cache_key = embeddedFontCacheKey(embedded_key, selected->font_size, bold, italic);
        if (const auto cached = registry.rasterized_cache.find(cache_key); cached != registry.rasterized_cache.end()) {
            return cached->second;
        }
        const auto* ttf_bytes = selected->variants->get(bold, italic);
        if (ttf_bytes != nullptr) {
            auto rasterized = font::TtfBitmapRasterizer::rasterize(*ttf_bytes, selected->font_size, font_name);
            if (rasterized != nullptr) {
                if (cache) {
                    registry.rasterized_cache[cache_key] = rasterized;
                }
                return rasterized;
            }
        }
    }
    if (auto windows_font = WindowsFontBundle::getFont(font_name, font_size, bold, italic); windows_font != nullptr) {
        return windows_font;
    }
    if (auto mac_font = MacFontBundle::getFont(font_name, font_size, bold, italic); mac_font != nullptr) {
        return mac_font;
    }
    return nullptr;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getEmbeddedBitmapFont(const std::string& font_name,
                                                                      int font_size,
                                                                      bool bold,
                                                                      bool italic,
                                                                      bool cache) {
    if (font_name.empty()) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);
    const auto embedded_key = registry.embedded_ttf_fonts.contains(key)
        ? key
        : [&registry, &font_name]() -> std::string {
              if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
                  mapped != registry.canonical_index.end() && registry.embedded_ttf_fonts.contains(mapped->second)) {
                  return mapped->second;
              }
              return {};
          }();
    if (embedded_key.empty()) {
        return nullptr;
    }

    const auto selected = selectEmbeddedTtf(registry.embedded_ttf_fonts.at(embedded_key), font_size);
    if (!selected.has_value() || selected->variants == nullptr) {
        return nullptr;
    }
    const auto cache_key = embeddedFontCacheKey(embedded_key, selected->font_size, bold, italic);
    if (const auto cached = registry.rasterized_cache.find(cache_key); cached != registry.rasterized_cache.end()) {
        return cached->second;
    }
    const auto* ttf_bytes = selected->variants->get(bold, italic);
    if (ttf_bytes == nullptr) {
        return nullptr;
    }
    auto rasterized = font::TtfBitmapRasterizer::rasterize(*ttf_bytes, selected->font_size, font_name);
    if (rasterized != nullptr) {
        if (cache) {
            registry.rasterized_cache[cache_key] = rasterized;
        }
    }
    return rasterized;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getPfrBitmapFont(const std::string& font_name,
                                                                 int font_size,
                                                                 bool cache) {
    if (font_name.empty() || font_size <= 0) {
        return nullptr;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    auto key = lowerAscii(font_name);
    auto parsed = registry.parsed_fonts.find(key);
    if (parsed == registry.parsed_fonts.end()) {
        if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
            mapped != registry.canonical_index.end()) {
            key = mapped->second;
            parsed = registry.parsed_fonts.find(key);
        }
    }
    if (parsed == registry.parsed_fonts.end()) {
        return nullptr;
    }

    const auto cache_key = fontCacheKey(key, font_size, true, false);
    if (const auto cached = registry.rasterized_cache.find(cache_key); cached != registry.rasterized_cache.end()) {
        return cached->second;
    }
    auto rasterized = font::BitmapFont::fromPfr1(*parsed->second, font_size);
    if (rasterized != nullptr && cache) {
        registry.rasterized_cache[cache_key] = rasterized;
    }
    return rasterized;
}

std::shared_ptr<font::BitmapFont> FontRegistry::getFallbackSelection(const std::string& font_name,
                                                                     int font_size,
                                                                     bool bold) {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto it = registry.fallback_selection_cache.find(fontCacheKey(font_name, font_size, bold, false));
    return it == registry.fallback_selection_cache.end() ? nullptr : it->second;
}

void FontRegistry::setFallbackSelection(const std::string& font_name,
                                        int font_size,
                                        bool bold,
                                        std::shared_ptr<font::BitmapFont> font) {
    if (font_name.empty() || font == nullptr) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.fallback_selection_cache[fontCacheKey(font_name, font_size, bold, false)] = std::move(font);
}

void FontRegistry::registerEmbeddedTtfFont(const std::string& font_name,
                                           std::vector<std::uint8_t> regular,
                                           std::vector<std::uint8_t> bold,
                                           std::vector<std::uint8_t> italic,
                                           std::vector<std::uint8_t> bold_italic) {
    registerEmbeddedTtfFont(font_name,
                            0,
                            std::move(regular),
                            std::move(bold),
                            std::move(italic),
                            std::move(bold_italic));
}

void FontRegistry::registerEmbeddedTtfFont(const std::string& font_name,
                                           int font_size,
                                           std::vector<std::uint8_t> regular,
                                           std::vector<std::uint8_t> bold,
                                           std::vector<std::uint8_t> italic,
                                           std::vector<std::uint8_t> bold_italic) {
    if (font_name.empty() || isBlank(font_name) || font_size < 0 || regular.empty()) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);
    registry.embedded_ttf_fonts[key][font_size] = EmbeddedTtfVariants{std::move(regular),
                                                                    std::move(bold),
                                                                    std::move(italic),
                                                                    std::move(bold_italic)};
    registry.canonical_index[canonicalFontName(font_name)] = key;
    if (!registry.first_embedded_font.has_value()) {
        registry.first_embedded_font = font_name;
    }
}

bool FontRegistry::hasEmbeddedBoldVariant(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);
    auto it = registry.embedded_ttf_fonts.find(key);
    if (it == registry.embedded_ttf_fonts.end()) {
        if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
            mapped != registry.canonical_index.end()) {
            it = registry.embedded_ttf_fonts.find(mapped->second);
        }
    }
    return it != registry.embedded_ttf_fonts.end() &&
           std::any_of(it->second.begin(), it->second.end(), [](const auto& entry) {
               return entry.second.hasBold();
           });
}

void FontRegistry::registerFontAlias(const std::string& alias,
                                     const std::string& font_name,
                                     bool bold) {
    if (alias.empty() || isBlank(alias) || font_name.empty() || isBlank(font_name)) {
        return;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(alias);
    registry.aliases[key] = FontAlias{font_name, bold};
    registry.canonical_index[canonicalFontName(alias)] = key;
}

std::optional<FontRegistry::FontAlias> FontRegistry::getFontAlias(const std::string& font_name) {
    if (font_name.empty()) {
        return std::nullopt;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.aliases.find(lowerAscii(font_name)); it != registry.aliases.end()) {
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

std::optional<std::string> FontRegistry::resolveFont(const std::string& font_name) {
    if (font_name.empty()) {
        return std::nullopt;
    }

    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);

    if (registry.parsed_fonts.contains(key)) {
        return key;
    }

    if (const auto mapped = registry.canonical_index.find(canonicalFontName(font_name));
        mapped != registry.canonical_index.end()) {
        return mapped->second;
    }

    if (key.size() <= 3) {
        for (const auto& [font_key, _] : registry.parsed_fonts) {
            if (font_key.starts_with(key)) {
                return font_key;
            }
        }
    }

    return std::nullopt;
}

bool FontRegistry::hasPfrFont(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    const auto key = lowerAscii(font_name);
    return registry.parsed_fonts.contains(key);
}

std::optional<std::string> FontRegistry::getFirstRegisteredFont() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    return registry.first_registered_font;
}

std::optional<std::string> FontRegistry::getPreferredDirectorPixelFont() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    if (registry.first_embedded_font.has_value() && !registry.first_embedded_font->empty()) {
        return registry.first_embedded_font;
    }
    return registry.first_registered_font;
}

void FontRegistry::clear() {
    auto& registry = state();
    std::lock_guard lock(registry.mutex);
    registry.parsed_fonts.clear();
    registry.ttf_cache.clear();
    registry.rasterized_cache.clear();
    registry.fallback_selection_cache.clear();
    registry.embedded_ttf_fonts.clear();
    registry.canonical_index.clear();
    registry.aliases.clear();
    registry.first_registered_font.reset();
    registry.first_embedded_font.reset();
    seedDefaultEmbeddedFontsLocked(registry);
}

} // namespace libreshockwave::player::cast
