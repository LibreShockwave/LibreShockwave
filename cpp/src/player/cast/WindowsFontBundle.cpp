#include "libreshockwave/player/cast/WindowsFontBundle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/font/TtfBitmapRasterizer.hpp"
#include "libreshockwave/fonts/PlatformFonts.hpp"

namespace libreshockwave::player::cast {
namespace {

struct FontVariants {
    std::vector<std::uint8_t> regular;
    std::vector<std::uint8_t> bold;
    std::vector<std::uint8_t> italic;
    std::vector<std::uint8_t> boldItalic;

    [[nodiscard]] const std::vector<std::uint8_t>* get(int variantIndex) const {
        const std::vector<std::uint8_t>* requested = nullptr;
        switch (variantIndex) {
            case 1:
                requested = &bold;
                break;
            case 2:
                requested = &italic;
                break;
            case 3:
                requested = &boldItalic;
                break;
            default:
                requested = &regular;
                break;
        }
        return requested != nullptr && !requested->empty()
            ? requested
            : (regular.empty() ? nullptr : &regular);
    }
};

struct WindowsFontState {
    std::unordered_map<std::string, FontVariants> fontData;
    std::unordered_map<std::string, std::shared_ptr<font::BitmapFont>> cache;
    std::mutex mutex;
};

WindowsFontState& state() {
    static WindowsFontState bundle;
    return bundle;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const std::unordered_map<std::string, std::array<bool, 4>>& knownFonts() {
    static const std::unordered_map<std::string, std::array<bool, 4>> fonts{
        {"verdana", {true, true, true, true}},
        {"arial", {true, false, false, false}},
        {"courier new", {true, false, false, false}},
        {"times new roman", {true, true, true, true}},
    };
    return fonts;
}

const std::unordered_map<std::string, std::array<std::string_view, 4>>& bundledFontDataKeys() {
    static const std::unordered_map<std::string, std::array<std::string_view, 4>> keys{
        {"verdana", {"verdana", "verdanabd", "verdanait", "verdanabdit"}},
        {"arial", {"arial", "", "", ""}},
        {"courier new", {"couriernew", "", "", ""}},
        {"times new roman", {"timesnewroman", "timesnewromanbd", "timesnewromanit", "timesnewromanbdit"}},
    };
    return keys;
}

std::string cacheKey(const std::string& fontName, int fontSize, int variantIndex) {
    return lowerAscii(fontName) + ":" + std::to_string(fontSize) + ":" + std::to_string(variantIndex);
}

const std::vector<std::uint8_t>* bundledBytesFor(const std::string& fontKey,
                                                int requestedVariant,
                                                int& resolvedVariant) {
    const auto dataKeys = bundledFontDataKeys().find(fontKey);
    if (dataKeys == bundledFontDataKeys().end()) {
        return nullptr;
    }

    const int variantIndex = requestedVariant >= 0 && requestedVariant < 4 ? requestedVariant : 0;
    const auto requestedKey = dataKeys->second[static_cast<std::size_t>(variantIndex)];
    if (!requestedKey.empty()) {
        if (const auto* requestedBytes = fonts::platform::windowsFontData(requestedKey); requestedBytes != nullptr) {
            resolvedVariant = variantIndex;
            return requestedBytes;
        }
    }

    const auto regularKey = dataKeys->second[0];
    if (!regularKey.empty()) {
        if (const auto* regularBytes = fonts::platform::windowsFontData(regularKey); regularBytes != nullptr) {
            resolvedVariant = 0;
            return regularBytes;
        }
    }
    return nullptr;
}

} // namespace

void WindowsFontBundle::registerFontData(const std::string& fontName,
                                         std::vector<std::uint8_t> regular,
                                         std::vector<std::uint8_t> bold,
                                         std::vector<std::uint8_t> italic,
                                         std::vector<std::uint8_t> boldItalic) {
    if (fontName.empty() || regular.empty()) {
        return;
    }
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.fontData[lowerAscii(fontName)] = FontVariants{std::move(regular),
                                                         std::move(bold),
                                                         std::move(italic),
                                                         std::move(boldItalic)};
    bundle.cache.clear();
}

void WindowsFontBundle::clearFontData() {
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.fontData.clear();
    bundle.cache.clear();
}

std::shared_ptr<font::BitmapFont> WindowsFontBundle::getFont(const std::string& fontName,
                                                             int fontSize,
                                                             bool bold,
                                                             bool italic) {
    if (fontName.empty()) {
        return nullptr;
    }

    const int requestedVariant = (bold ? 1 : 0) + (italic ? 2 : 0);
    const auto key = lowerAscii(fontName);
    const auto requestedCacheKey = cacheKey(fontName, fontSize, requestedVariant);
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto cached = bundle.cache.find(requestedCacheKey); cached != bundle.cache.end()) {
            return cached->second;
        }
    }

    std::vector<std::uint8_t> customBytes;
    const std::vector<std::uint8_t>* selectedBytes = nullptr;
    int resolvedVariant = requestedVariant;
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        const auto variants = bundle.fontData.find(key);
        if (variants != bundle.fontData.end()) {
            const auto* selected = variants->second.get(requestedVariant);
            if (selected != nullptr) {
                customBytes = *selected;
                selectedBytes = &customBytes;
                if (selected == &variants->second.regular) {
                    resolvedVariant = 0;
                }
            }
        }
    }

    if (selectedBytes == nullptr) {
        selectedBytes = bundledBytesFor(key, requestedVariant, resolvedVariant);
    }
    if (selectedBytes == nullptr) {
        return nullptr;
    }

    auto loaded = font::TtfBitmapRasterizer::rasterize(*selectedBytes, fontSize, fontName);
    if (loaded == nullptr) {
        return nullptr;
    }

    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.cache[requestedCacheKey] = loaded;
    if (resolvedVariant != requestedVariant) {
        bundle.cache[cacheKey(fontName, fontSize, resolvedVariant)] = loaded;
    }
    return loaded;
}

bool WindowsFontBundle::hasWindowsFont(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    const auto key = lowerAscii(fontName);
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    return bundle.fontData.contains(key) || knownFonts().contains(key);
}

bool WindowsFontBundle::hasBoldVariant(const std::string& fontName) {
    if (fontName.empty()) {
        return false;
    }
    const auto key = lowerAscii(fontName);
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    if (const auto it = bundle.fontData.find(key); it != bundle.fontData.end()) {
        return !it->second.bold.empty();
    }
    if (const auto known = knownFonts().find(key); known != knownFonts().end()) {
        return (*known).second[1];
    }
    return false;
}

} // namespace libreshockwave::player::cast
