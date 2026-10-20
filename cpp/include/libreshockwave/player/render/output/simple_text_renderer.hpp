#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/font/bitmap_font.hpp"
#include "libreshockwave/player/render/output/text_renderer.hpp"

namespace libreshockwave::player::render::output {

class SimpleTextRenderer final : public TextRenderer {
public:
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderText(
        std::string text,
        int width,
        int height,
        std::string font_name,
        int font_size,
        std::string font_style,
        std::string alignment,
        int text_color,
        int bg_color,
        bool word_wrap,
        bool antialias,
        int fixed_line_space,
        int top_spacing) override;

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderTextInField(
        std::string text,
        int width,
        int height,
        std::string font_name,
        int font_size,
        std::string font_style,
        std::string alignment,
        int text_color,
        int bg_color,
        bool word_wrap,
        bool antialias,
        int fixed_line_space,
        int top_spacing,
        int text_gutter) override;

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderLegacyStxtText(
        std::string text,
        int width,
        int height,
        std::string font_name,
        int font_size,
        std::string font_style,
        std::string alignment,
        int text_color,
        int bg_color,
        bool word_wrap,
        bool antialias,
        int fixed_line_space,
        int top_spacing);

    [[nodiscard]] std::vector<int> charPosToLoc(std::string text,
                                                int char_index,
                                                std::string font_name,
                                                int font_size,
                                                std::string font_style,
                                                int fixed_line_space,
                                                std::string alignment,
                                                int field_width) override;

    [[nodiscard]] int locToCharPos(std::string text,
                                   int x,
                                   int y,
                                   std::string font_name,
                                   int font_size,
                                   std::string font_style,
                                   int fixed_line_space,
                                   std::string alignment,
                                   int field_width) override;

    [[nodiscard]] int getLineHeight(std::string font_name,
                                    int font_size,
                                    std::string font_style,
                                    int fixed_line_space) override;

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderXmedText(
        const ::libreshockwave::cast::XmedStyledText* styled_text,
        int width,
        int height,
        int text_color,
        int bg_color) override;

private:
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> renderTextInternal(
        std::string text,
        int width,
        int height,
        std::string font_name,
        int font_size,
        std::string font_style,
        std::string alignment,
        int text_color,
        int bg_color,
        bool word_wrap,
        bool antialias,
        int fixed_line_space,
        int top_spacing,
        bool prefer_registered_director_fonts,
        int wrap_width = -1);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveBitmapFont(
        const std::string& font_name,
        int font_size,
        bool bold = false,
        bool italic = false,
        bool* used_real_bold = nullptr,
        bool prefer_registered_director_fonts = false);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveDirectorFontAlias(
        const std::string& font_name,
        int font_size,
        bool bold,
        bool italic,
        bool* used_real_bold,
        bool prefer_mac_fonts,
        bool prefer_registered_director_fonts);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveRegisteredDirectorFont(
        const std::string& original_name,
        const std::string& resolved_name,
        int font_size,
        bool bold,
        bool italic,
        bool* used_real_bold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveRegisteredPfrCandidate(
        const std::string& font_name,
        int font_size);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveXmedFont(
        const ::libreshockwave::cast::XmedStyledText& styled_text,
        int font_size,
        bool bold,
        bool italic,
        bool* used_real_bold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveXmedFontByName(
        const std::string& font_name,
        const std::vector<std::string>* font_candidates,
        int font_size,
        bool bold,
        bool italic,
        bool* used_real_bold);

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> resolveMovieFontCandidate(
        const std::vector<std::string>* font_candidates,
        const std::string& primary_font_name,
        int font_size,
        bool bold,
        bool italic,
        bool* used_real_bold);

    [[nodiscard]] static int directorAliasFontSize(int font_size);
};

} // namespace libreshockwave::player::render::output
