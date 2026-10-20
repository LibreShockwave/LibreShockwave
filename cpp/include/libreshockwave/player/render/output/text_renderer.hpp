#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/xmed_styled_text.hpp"

namespace libreshockwave::player::render::output {

class TextRenderer {
public:
    virtual ~TextRenderer() = default;

    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderText(
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
        int top_spacing) = 0;

    // Render a Director text member into its full field rectangle. The field
    // width includes the member's inner gutter, while the returned bitmap
    // retains the full authored dimensions.
    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderTextInField(
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
        int text_gutter) {
        (void)text_gutter;
        return renderText(std::move(text),
                          width,
                          height,
                          std::move(font_name),
                          font_size,
                          std::move(font_style),
                          std::move(alignment),
                          text_color,
                          bg_color,
                          word_wrap,
                          antialias,
                          fixed_line_space,
                          top_spacing);
    }

    [[nodiscard]] virtual std::vector<int> charPosToLoc(std::string text,
                                                        int char_index,
                                                        std::string font_name,
                                                        int font_size,
                                                        std::string font_style,
                                                        int fixed_line_space,
                                                        std::string alignment,
                                                        int field_width) = 0;

    [[nodiscard]] virtual int locToCharPos(std::string text,
                                           int x,
                                           int y,
                                           std::string font_name,
                                           int font_size,
                                           std::string font_style,
                                           int fixed_line_space,
                                           std::string alignment,
                                           int field_width) = 0;

    [[nodiscard]] virtual int getLineHeight(std::string font_name,
                                            int font_size,
                                            std::string font_style,
                                            int fixed_line_space) = 0;

    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderXmedText(
        const ::libreshockwave::cast::XmedStyledText* styled_text,
        int width,
        int height,
        int text_color,
        int bg_color);

    [[nodiscard]] static std::vector<std::string> splitLines(std::string_view text);
    [[nodiscard]] static std::vector<int> findCharLine(std::string_view text, int char_index);
    [[nodiscard]] static int lineStartIndex(std::string_view text, int target_line);
    static void wrapLine(std::string_view text,
                         const std::function<int(std::string_view)>& measure_width,
                         int max_width,
                         std::vector<std::string>& out);

private:
    [[nodiscard]] static bool appendHyphenSplitIfFits(std::string& current,
                                                      std::string_view word,
                                                      const std::function<int(std::string_view)>& measure_width,
                                                      int max_width,
                                                      std::vector<std::string>& out);
};

} // namespace libreshockwave::player::render::output
