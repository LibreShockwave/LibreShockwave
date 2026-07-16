#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/font/BitmapFont.hpp"
#include "libreshockwave/font/Pfr1Font.hpp"

namespace libreshockwave::player::cast {

class FontRegistry {
public:
    struct FontAlias {
        std::string fontName;
        bool bold = false;
    };

    static void registerBitmapFont(const std::string& fontName,
                                   int fontSize,
                                   std::shared_ptr<font::BitmapFont> font);
    static void registerPfr1Font(const std::string& memberName,
                                 const std::vector<std::uint8_t>& pfrData);
    [[nodiscard]] static std::shared_ptr<font::Pfr1Font> getPfr1Font(const std::string& fontName);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> getTtfBytes(const std::string& fontName);
    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getBitmapFont(const std::string& fontName,
                                                                         int fontSize);
    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getBitmapFont(const std::string& fontName,
                                                                         int fontSize,
                                                                         bool bold,
                                                                         bool italic,
                                                                         bool cache = true);
    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getEmbeddedBitmapFont(const std::string& fontName,
                                                                                 int fontSize,
                                                                                 bool bold,
                                                                                 bool italic,
                                                                                 bool cache = true);
    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getPfrBitmapFont(const std::string& fontName,
                                                                            int fontSize,
                                                                            bool cache = true);

    // Memoization for the preferred-Director-pixel-font fallback search in SimpleTextRenderer.
    // The fallback rasterizes a small embedded pixel font at many candidate sizes to find the
    // closest ink-height match; without memoization that O(fontSize) probe re-runs on every text
    // sprite and, when cached, balloons memory (a size-256 request cached 258 atlases ~15GB).
    // The memo stores the single selected font per (fontName, fontSize, bold) so the probe runs
    // once per distinct size; entries share FontRegistry's lifetime and are cleared with it.
    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getFallbackSelection(const std::string& fontName,
                                                                                int fontSize,
                                                                                bool bold);
    static void setFallbackSelection(const std::string& fontName,
                                     int fontSize,
                                     bool bold,
                                     std::shared_ptr<font::BitmapFont> font);
    static void registerEmbeddedTtfFont(const std::string& fontName,
                                        std::vector<std::uint8_t> regular,
                                        std::vector<std::uint8_t> bold = {},
                                        std::vector<std::uint8_t> italic = {},
                                        std::vector<std::uint8_t> boldItalic = {});
    static void registerEmbeddedTtfFont(const std::string& fontName,
                                        int fontSize,
                                        std::vector<std::uint8_t> regular,
                                        std::vector<std::uint8_t> bold = {},
                                        std::vector<std::uint8_t> italic = {},
                                        std::vector<std::uint8_t> boldItalic = {});
    [[nodiscard]] static bool hasEmbeddedBoldVariant(const std::string& fontName);

    static void registerFontAlias(const std::string& alias,
                                  const std::string& fontName,
                                  bool bold);
    [[nodiscard]] static std::optional<FontAlias> getFontAlias(const std::string& fontName);

    [[nodiscard]] static std::string canonicalFontName(const std::string& name);
    [[nodiscard]] static std::optional<std::string> resolveFont(const std::string& fontName);
    [[nodiscard]] static bool hasPfrFont(const std::string& fontName);
    [[nodiscard]] static std::optional<std::string> getFirstRegisteredFont();
    [[nodiscard]] static std::optional<std::string> getPreferredDirectorPixelFont();

    static void clear();
};

} // namespace libreshockwave::player::cast
