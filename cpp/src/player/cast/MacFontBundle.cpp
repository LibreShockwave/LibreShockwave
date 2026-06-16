#include "libreshockwave/player/cast/MacFontBundle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "libreshockwave/font/TtfBitmapRasterizer.hpp"
#include "libreshockwave/fonts/PlatformFonts.hpp"
#include "libreshockwave/player/cast/FontRegistry.hpp"

namespace libreshockwave::player::cast {
namespace {

struct FontFamily {
    std::string filePrefix;
    std::vector<int> sizes;
    std::string boldFilePrefix;
    std::vector<int> boldSizes;
};

struct MacFontState {
    std::unordered_map<std::string, std::vector<std::uint8_t>> ttfData;
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

std::string resolveFamilyKey(const std::string& fontName) {
    auto key = lowerAscii(fontName);
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
    int bestDifference = std::numeric_limits<int>::max();
    for (const int size : sizes) {
        const int difference = std::abs(size - requested);
        if (difference < bestDifference) {
            bestDifference = difference;
            best = size;
        }
    }
    return best;
}

std::string cacheKey(const std::string& familyKey, int size, bool bold) {
    return familyKey + ":" + std::to_string(size) + ":" + (bold ? "bold" : "regular");
}

std::string dataKey(const std::string& filePrefix, int size) {
    return lowerAscii(filePrefix + "-" + std::to_string(size));
}

std::shared_ptr<font::BitmapFont> loadTtf(const std::string& filePrefix,
                                          int size,
                                          const std::string& familyKey) {
    const auto key = dataKey(filePrefix, size);
    std::vector<std::uint8_t> bytes;
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto it = bundle.ttfData.find(key); it != bundle.ttfData.end()) {
            bytes = it->second;
        }
    }
    if (!bytes.empty()) {
        return font::TtfBitmapRasterizer::rasterize(bytes, size, familyKey);
    }
    if (const auto* bundledBytes = fonts::platform::macFontData(key); bundledBytes != nullptr) {
        return font::TtfBitmapRasterizer::rasterize(*bundledBytes, size, familyKey);
    }
    return nullptr;
}

} // namespace

void MacFontBundle::initialize() {
    for (const auto& [familyKey, family] : fontFamilies()) {
        for (const int size : family.sizes) {
            auto loaded = loadTtf(family.filePrefix, size, familyKey);
            if (loaded != nullptr) {
                FontRegistry::registerBitmapFont(familyKey, size, loaded);
            }
        }
    }
}

void MacFontBundle::registerTtfData(const std::string& key, std::vector<std::uint8_t> ttfBytes) {
    if (key.empty() || ttfBytes.empty()) {
        return;
    }
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.ttfData[lowerAscii(key)] = std::move(ttfBytes);
    bundle.cache.clear();
}

void MacFontBundle::clearTtfData() {
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.ttfData.clear();
    bundle.cache.clear();
}

std::shared_ptr<font::BitmapFont> MacFontBundle::getFont(const std::string& fontName,
                                                         int fontSize,
                                                         bool bold,
                                                         bool /*italic*/) {
    if (fontName.empty()) {
        return nullptr;
    }

    const auto familyKey = resolveFamilyKey(fontName);
    const auto familyIt = fontFamilies().find(familyKey);
    if (familyIt == fontFamilies().end()) {
        return nullptr;
    }
    const auto& family = familyIt->second;

    const bool useBold = bold && !family.boldFilePrefix.empty();
    const auto& sizes = useBold ? family.boldSizes : family.sizes;
    const int bestSize = findBestSize(sizes, fontSize);
    if (bestSize <= 0) {
        return nullptr;
    }

    const auto key = cacheKey(familyKey, bestSize, useBold);
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto cached = bundle.cache.find(key); cached != bundle.cache.end()) {
            return cached->second;
        }
    }

    const auto& filePrefix = useBold ? family.boldFilePrefix : family.filePrefix;
    auto loaded = loadTtf(filePrefix, bestSize, familyKey);
    if (loaded == nullptr) {
        return nullptr;
    }

    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.cache[key] = loaded;
    return loaded;
}

bool MacFontBundle::hasMacFont(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    return fontFamilies().contains(resolveFamilyKey(fontName));
}

bool MacFontBundle::hasBoldVariant(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    const auto familyIt = fontFamilies().find(resolveFamilyKey(fontName));
    return familyIt != fontFamilies().end() && !familyIt->second.boldFilePrefix.empty();
}

} // namespace libreshockwave::player::cast
