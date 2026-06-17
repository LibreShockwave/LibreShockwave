#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/font/BitmapFont.hpp"
#include "libreshockwave/player/render/output/TextRenderer.hpp"

namespace libreshockwave::player::render::output {

class SimpleTextRenderer final : public TextRenderer {
public:
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderText(
        std::string text,
        int width,
        int height,
        std::string fontName,
        int fontSize,
        std::string fontStyle,
        std::string alignment,
        int textColor,
        int bgColor,
        bool wordWrap,
        bool antialias,
        int fixedLineSpace,
        int topSpacing) override;

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderLegacyStxtText(
        std::string text,
        int width,
        int height,
        std::string fontName,
        int fontSize,
        std::string fontStyle,
        std::string alignment,
        int textColor,
        int bgColor,
        bool wordWrap,
        bool antialias,
        int fixedLineSpace,
        int topSpacing);

    [[nodiscard]] std::vector<int> charPosToLoc(std::string text,
                                                int charIndex,
                                                std::string fontName,
                                                int fontSize,
                                                std::string fontStyle,
                                                int fixedLineSpace,
                                                std::string alignment,
                                                int fieldWidth) override;

    [[nodiscard]] int locToCharPos(std::string text,
                                   int x,
                                   int y,
                                   std::string fontName,
                                   int fontSize,
                                   std::string fontStyle,
                                   int fixedLineSpace,
                                   std::string alignment,
                                   int fieldWidth) override;

    [[nodiscard]] int getLineHeight(std::string fontName,
                                    int fontSize,
                                    std::string fontStyle,
                                    int fixedLineSpace) override;

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderXmedText(
        const ::libreshockwave::cast::XmedStyledText* styledText,
        int width,
        int height,
        int textColor,
        int bgColor) override;

private:
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderTextInternal(
        std::string text,
        int width,
        int height,
        std::string fontName,
        int fontSize,
        std::string fontStyle,
        std::string alignment,
        int textColor,
        int bgColor,
        bool wordWrap,
        bool antialias,
        int fixedLineSpace,
        int topSpacing,
        bool preferRegisteredDirectorFonts);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveBitmapFont(
        const std::string& fontName,
        int fontSize,
        bool bold = false,
        bool italic = false,
        bool* usedRealBold = nullptr,
        bool preferRegisteredDirectorFonts = false);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveDirectorFontAlias(
        const std::string& fontName,
        int fontSize,
        bool bold,
        bool italic,
        bool* usedRealBold,
        bool preferMacFonts,
        bool preferRegisteredDirectorFonts);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveRegisteredDirectorFont(
        const std::string& originalName,
        const std::string& resolvedName,
        int fontSize,
        bool bold,
        bool italic,
        bool* usedRealBold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveRegisteredPfrCandidate(
        const std::string& fontName,
        int fontSize);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveXmedFont(
        const ::libreshockwave::cast::XmedStyledText& styledText,
        int fontSize,
        bool bold,
        bool italic,
        bool* usedRealBold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveXmedFontByName(
        const std::string& fontName,
        const std::vector<std::string>* fontCandidates,
        int fontSize,
        bool bold,
        bool italic,
        bool* usedRealBold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveMovieFontCandidate(
        const std::vector<std::string>* fontCandidates,
        const std::string& primaryFontName,
        int fontSize,
        bool bold,
        bool italic,
        bool* usedRealBold);

    [[nodiscard]] static int directorAliasFontSize(int fontSize);
};

} // namespace libreshockwave::player::render::output
