#include "libreshockwave/player/cast/mac_font_bundle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "libreshockwave/font/ttf_bitmap_rasterizer.hpp"
#include "libreshockwave/fonts/platform_fonts.hpp"
#include "libreshockwave/player/cast/font_registry.hpp"

namespace libreshockwave::player::cast {
namespace {

struct FontFamily {
    std::string file_prefix;
    std::vector<int> sizes;
    std::string bold_file_prefix;
    std::vector<int> bold_sizes;
};

struct MacFontState {
    std::unordered_map<std::string, std::vector<std::uint8_t>> ttf_data;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> cache;
    std::mutex mutex;
};

MacFontState& state() {
    static MacFontState bundle;
    return bundle;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const std::map<std::string, FontFamily>& fontFamilies() {
    static const std::map<std::string, FontFamily> families{
        {"geneva", {"Geneva", {9, 10, 12, 14, 18, 20}, {}, {}}},
        {"chicago", {"Chicago", {12}, {}, {}}},
        {"monaco", {"Monaco", {9, 12}, {}, {}}},
        {"helvetica", {"Helvetica", {9, 10, 12, 14, 18, 24}, {}, {}}},
        {"courier", {"Courier", {9, 10, 12, 14, 18, 24}, {}, {}}},
        {"times", {"Times", {9, 10, 12, 14, 18, 24}, {}, {}}},
        {"palatino", {"Palatino", {10, 12, 14, 18, 24}, {}, {}}},
        {"espysans", {"EspySans", {9, 10, 12, 14, 16}, "EspySansBold", {9, 10, 12, 14, 16}}},
        {"espyserif", {"EspySerif", {10, 12, 14, 16}, "EspySerifBold", {10, 12, 14, 16}}},
    };
    return families;
}

const std::unordered_map<std::string, std::string>& fontAliases() {
    static const std::unordered_map<std::string, std::string> aliases{
        {"verdana", "geneva"},
        {"arial", "helvetica"},
        {"courier new", "courier"},
        {"times new roman", "times"},
    };
    return aliases;
}

std::string resolveFamilyKey(const std::string& font_name) {
    auto key = lowerAscii(font_name);
    if (const auto alias = fontAliases().find(key); alias != fontAliases().end()) {
        key = alias->second;
    }
    return key;
}

int findBestSize(const std::vector<int>& sizes, int requested) {
    if (sizes.empty()) {
        return -1;
    }
    if (std::find(sizes.begin(), sizes.end(), requested) != sizes.end()) {
        return requested;
    }

    int best = -1;
    int best_difference = std::numeric_limits<int>::max();
    for (const int size : sizes) {
        const int difference = std::abs(size - requested);
        if (difference < best_difference) {
            best_difference = difference;
            best = size;
        }
    }
    return best;
}

std::string cacheKey(const std::string& family_key, int size, bool bold) {
    return family_key + ":" + std::to_string(size) + ":" + (bold ? "bold" : "regular");
}

std::string dataKey(const std::string& file_prefix, int size) {
    return lowerAscii(file_prefix + "-" + std::to_string(size));
}

std::shared_ptr<font::BitmapFont> loadTtf(const std::string& file_prefix,
                                          int size,
                                          const std::string& family_key) {
    const auto key = dataKey(file_prefix, size);
    std::vector<std::uint8_t> bytes;
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto it = bundle.ttf_data.find(key); it != bundle.ttf_data.end()) {
            bytes = it->second;
        }
    }
    if (!bytes.empty()) {
        return font::TtfBitmapRasterizer::rasterize(bytes, size, family_key);
    }
    if (const auto* bundled_bytes = fonts::platform::macFontData(key); bundled_bytes != nullptr) {
        return font::TtfBitmapRasterizer::rasterize(*bundled_bytes, size, family_key);
    }
    return nullptr;
}

} // namespace

void MacFontBundle::initialize() {
    for (const auto& [family_key, family] : fontFamilies()) {
        for (const int size : family.sizes) {
            auto loaded = loadTtf(family.file_prefix, size, family_key);
            if (loaded != nullptr) {
                FontRegistry::registerBitmapFont(family_key, size, loaded);
            }
        }
    }
}

void MacFontBundle::registerTtfData(const std::string& key, std::vector<std::uint8_t> ttf_bytes) {
    if (key.empty() || ttf_bytes.empty()) {
        return;
    }
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.ttf_data[lowerAscii(key)] = std::move(ttf_bytes);
    bundle.cache.clear();
}

void MacFontBundle::clearTtfData() {
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.ttf_data.clear();
    bundle.cache.clear();
}

std::shared_ptr<font::BitmapFont> MacFontBundle::getFont(const std::string& font_name,
                                                         int font_size,
                                                         bool bold,
                                                         bool /*italic*/) {
    if (font_name.empty()) {
        return nullptr;
    }

    const auto family_key = resolveFamilyKey(font_name);
    const auto family_it = fontFamilies().find(family_key);
    if (family_it == fontFamilies().end()) {
        return nullptr;
    }
    const auto& family = family_it->second;

    const bool use_bold = bold && !family.bold_file_prefix.empty();
    const auto& sizes = use_bold ? family.bold_sizes : family.sizes;
    const int best_size = findBestSize(sizes, font_size);
    if (best_size <= 0) {
        return nullptr;
    }

    const auto key = cacheKey(family_key, best_size, use_bold);
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto cached = bundle.cache.find(key); cached != bundle.cache.end()) {
            return cached->second;
        }
    }

    const auto& file_prefix = use_bold ? family.bold_file_prefix : family.file_prefix;
    auto loaded = loadTtf(file_prefix, best_size, family_key);
    if (loaded == nullptr) {
        return nullptr;
    }

    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.cache[key] = loaded;
    return loaded;
}

bool MacFontBundle::hasMacFont(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    return fontFamilies().contains(resolveFamilyKey(font_name));
}

bool MacFontBundle::hasBoldVariant(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    const auto family_it = fontFamilies().find(resolveFamilyKey(font_name));
    return family_it != fontFamilies().end() && !family_it->second.bold_file_prefix.empty();
}

} // namespace libreshockwave::player::cast
