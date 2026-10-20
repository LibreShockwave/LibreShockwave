#include "libreshockwave/player/cast/windows_font_bundle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/font/ttf_bitmap_rasterizer.hpp"
#include "libreshockwave/fonts/platform_fonts.hpp"

namespace libreshockwave::player::cast {
namespace {

struct FontVariants {
    std::vector<std::uint8_t> regular;
    std::vector<std::uint8_t> bold;
    std::vector<std::uint8_t> italic;
    std::vector<std::uint8_t> bold_italic;

    [[nodiscard]] const std::vector<std::uint8_t>* get(int variant_index) const {
        const std::vector<std::uint8_t>* requested = nullptr;
        switch (variant_index) {
            case 1:
                requested = &bold;
                break;
            case 2:
                requested = &italic;
                break;
            case 3:
                requested = &bold_italic;
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
    std::unordered_map<std::string, FontVariants> font_data;
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

std::string cacheKey(const std::string& font_name, int font_size, int variant_index) {
    return lowerAscii(font_name) + ":" + std::to_string(font_size) + ":" + std::to_string(variant_index);
}

const std::vector<std::uint8_t>* bundledBytesFor(const std::string& font_key,
                                                int requested_variant,
                                                int& resolved_variant) {
    const auto data_keys = bundledFontDataKeys().find(font_key);
    if (data_keys == bundledFontDataKeys().end()) {
        return nullptr;
    }

    const int variant_index = requested_variant >= 0 && requested_variant < 4 ? requested_variant : 0;
    const auto requested_key = data_keys->second[static_cast<std::size_t>(variant_index)];
    if (!requested_key.empty()) {
        if (const auto* requested_bytes = fonts::platform::windowsFontData(requested_key); requested_bytes != nullptr) {
            resolved_variant = variant_index;
            return requested_bytes;
        }
    }

    const auto regular_key = data_keys->second[0];
    if (!regular_key.empty()) {
        if (const auto* regular_bytes = fonts::platform::windowsFontData(regular_key); regular_bytes != nullptr) {
            resolved_variant = 0;
            return regular_bytes;
        }
    }
    return nullptr;
}

} // namespace

void WindowsFontBundle::registerFontData(const std::string& font_name,
                                         std::vector<std::uint8_t> regular,
                                         std::vector<std::uint8_t> bold,
                                         std::vector<std::uint8_t> italic,
                                         std::vector<std::uint8_t> bold_italic) {
    if (font_name.empty() || regular.empty()) {
        return;
    }
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.font_data[lowerAscii(font_name)] = FontVariants{std::move(regular),
                                                         std::move(bold),
                                                         std::move(italic),
                                                         std::move(bold_italic)};
    bundle.cache.clear();
}

void WindowsFontBundle::clearFontData() {
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.font_data.clear();
    bundle.cache.clear();
}

std::shared_ptr<font::BitmapFont> WindowsFontBundle::getFont(const std::string& font_name,
                                                             int font_size,
                                                             bool bold,
                                                             bool italic) {
    if (font_name.empty()) {
        return nullptr;
    }

    const int requested_variant = (bold ? 1 : 0) + (italic ? 2 : 0);
    const auto key = lowerAscii(font_name);
    const auto requested_cache_key = cacheKey(font_name, font_size, requested_variant);
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        if (const auto cached = bundle.cache.find(requested_cache_key); cached != bundle.cache.end()) {
            return cached->second;
        }
    }

    std::vector<std::uint8_t> custom_bytes;
    const std::vector<std::uint8_t>* selected_bytes = nullptr;
    int resolved_variant = requested_variant;
    {
        auto& bundle = state();
        std::lock_guard lock(bundle.mutex);
        const auto variants = bundle.font_data.find(key);
        if (variants != bundle.font_data.end()) {
            const auto* selected = variants->second.get(requested_variant);
            if (selected != nullptr) {
                custom_bytes = *selected;
                selected_bytes = &custom_bytes;
                if (selected == &variants->second.regular) {
                    resolved_variant = 0;
                }
            }
        }
    }

    if (selected_bytes == nullptr) {
        selected_bytes = bundledBytesFor(key, requested_variant, resolved_variant);
    }
    if (selected_bytes == nullptr) {
        return nullptr;
    }

    auto loaded = font::TtfBitmapRasterizer::rasterize(*selected_bytes, font_size, font_name);
    if (loaded == nullptr) {
        return nullptr;
    }

    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    bundle.cache[requested_cache_key] = loaded;
    if (resolved_variant != requested_variant) {
        bundle.cache[cacheKey(font_name, font_size, resolved_variant)] = loaded;
    }
    return loaded;
}

bool WindowsFontBundle::hasWindowsFont(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    const auto key = lowerAscii(font_name);
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    return bundle.font_data.contains(key) || knownFonts().contains(key);
}

bool WindowsFontBundle::hasBoldVariant(const std::string& font_name) {
    if (font_name.empty()) {
        return false;
    }
    const auto key = lowerAscii(font_name);
    auto& bundle = state();
    std::lock_guard lock(bundle.mutex);
    if (const auto it = bundle.font_data.find(key); it != bundle.font_data.end()) {
        return !it->second.bold.empty();
    }
    if (const auto known = knownFonts().find(key); known != knownFonts().end()) {
        return (*known).second[1];
    }
    return false;
}

} // namespace libreshockwave::player::cast
