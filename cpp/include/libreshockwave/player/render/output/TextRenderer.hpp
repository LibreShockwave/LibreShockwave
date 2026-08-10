#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/XmedStyledText.hpp"

namespace libreshockwave::player::render::output {

class TextRenderer {
public:
    virtual ~TextRenderer() = default;

    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderText(
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
        int topSpacing) = 0;

    // Render a Director text member into its full field rectangle. The field
    // width includes the member's inner gutter, while the returned bitmap
    // retains the full authored dimensions.
    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderTextInField(
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
        int textGutter) {
        (void)textGutter;
        return renderText(std::move(text),
                          width,
                          height,
                          std::move(fontName),
                          fontSize,
                          std::move(fontStyle),
                          std::move(alignment),
                          textColor,
                          bgColor,
                          wordWrap,
                          antialias,
                          fixedLineSpace,
                          topSpacing);
    }

    [[nodiscard]] virtual std::vector<int> charPosToLoc(std::string text,
                                                        int charIndex,
                                                        std::string fontName,
                                                        int fontSize,
                                                        std::string fontStyle,
                                                        int fixedLineSpace,
                                                        std::string alignment,
                                                        int fieldWidth) = 0;

    [[nodiscard]] virtual int locToCharPos(std::string text,
                                           int x,
                                           int y,
                                           std::string fontName,
                                           int fontSize,
                                           std::string fontStyle,
                                           int fixedLineSpace,
                                           std::string alignment,
                                           int fieldWidth) = 0;

    [[nodiscard]] virtual int getLineHeight(std::string fontName,
                                            int fontSize,
                                            std::string fontStyle,
                                            int fixedLineSpace) = 0;

    [[nodiscard]] virtual std::shared_ptr<bitmap::Bitmap> renderXmedText(
        const ::libreshockwave::cast::XmedStyledText* styledText,
        int width,
        int height,
        int textColor,
        int bgColor);

    [[nodiscard]] static std::vector<std::string> splitLines(std::string_view text);
    [[nodiscard]] static std::vector<int> findCharLine(std::string_view text, int charIndex);
    [[nodiscard]] static int lineStartIndex(std::string_view text, int targetLine);
    static void wrapLine(std::string_view text,
                         const std::function<int(std::string_view)>& measureWidth,
                         int maxWidth,
                         std::vector<std::string>& out);

private:
    [[nodiscard]] static bool appendHyphenSplitIfFits(std::string& current,
                                                      std::string_view word,
                                                      const std::function<int(std::string_view)>& measureWidth,
                                                      int maxWidth,
                                                      std::vector<std::string>& out);
};

} // namespace libreshockwave::player::render::output
