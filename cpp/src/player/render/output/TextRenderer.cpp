#include "libreshockwave/player/render/output/TextRenderer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace libreshockwave::player::render::output {

std::shared_ptr<bitmap::Bitmap> TextRenderer::renderXmedText(
    const ::libreshockwave::cast::XmedStyledText* styledText,
    int width,
    int height,
    int textColor,
    int bgColor) {
    if (styledText == nullptr) {
        return nullptr;
    }
    return renderText(styledText->text,
                      width,
                      height,
                      styledText->fontName,
                      styledText->fontSize,
                      styledText->fontStyleString(),
                      styledText->alignment,
                      textColor,
                      bgColor,
                      styledText->wordWrap,
                      styledText->antialias,
                      styledText->fixedLineSpace,
                      0);
}

std::vector<std::string> TextRenderer::splitLines(std::string_view text) {
    if (text.empty()) {
        return {""};
    }

    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t pos = 0; pos < text.size(); ++pos) {
        if (text[pos] == '\r') {
            lines.emplace_back(text.substr(start, pos - start));
            if (pos + 1 < text.size() && text[pos + 1] == '\n') {
                ++pos;
            }
            start = pos + 1;
        } else if (text[pos] == '\n') {
            lines.emplace_back(text.substr(start, pos - start));
            start = pos + 1;
        }
    }
    lines.emplace_back(text.substr(start));
    return lines;
}

std::vector<int> TextRenderer::findCharLine(std::string_view text, int charIndex) {
    if (text.empty()) {
        return {0, 0};
    }

    const int idx = std::max(0, std::min(charIndex - 1, static_cast<int>(text.size())));
    int lineNum = 0;
    int charsOnLine = 0;

    for (int pos = 0; pos < idx; ++pos) {
        const char ch = text[static_cast<std::size_t>(pos)];
        if (ch == '\r') {
            if ((pos + 1) < idx && (pos + 1) < static_cast<int>(text.size()) &&
                text[static_cast<std::size_t>(pos + 1)] == '\n') {
                ++pos;
            }
            ++lineNum;
            charsOnLine = 0;
        } else if (ch == '\n') {
            ++lineNum;
            charsOnLine = 0;
        } else {
            ++charsOnLine;
        }
    }

    return {lineNum, charsOnLine};
}

int TextRenderer::lineStartIndex(std::string_view text, int targetLine) {
    if (text.empty() || targetLine <= 0) {
        return 0;
    }

    int lineNum = 0;
    for (int pos = 0; pos < static_cast<int>(text.size()); ++pos) {
        const char ch = text[static_cast<std::size_t>(pos)];
        if (ch == '\r') {
            if ((pos + 1) < static_cast<int>(text.size()) && text[static_cast<std::size_t>(pos + 1)] == '\n') {
                ++pos;
            }
            ++lineNum;
            if (lineNum == targetLine) {
                return pos + 1;
            }
        } else if (ch == '\n') {
            ++lineNum;
            if (lineNum == targetLine) {
                return pos + 1;
            }
        }
    }
    return static_cast<int>(text.size());
}

void TextRenderer::wrapLine(std::string_view text,
                            const std::function<int(std::string_view)>& measureWidth,
                            int maxWidth,
                            std::vector<std::string>& out) {
    if (text.empty()) {
        out.emplace_back("");
        return;
    }
    if (measureWidth(text) <= maxWidth) {
        out.emplace_back(text);
        return;
    }

    std::istringstream wordsIn{std::string(text)};
    std::string word;
    std::string current;
    while (wordsIn >> word) {
        if (current.empty()) {
            current = word;
        } else {
            const std::string candidate = current + " " + word;
            if (measureWidth(candidate) <= maxWidth) {
                current = candidate;
            } else if (appendHyphenSplitIfFits(current, word, measureWidth, maxWidth, out)) {
            } else {
                out.push_back(current);
                current = word;
            }
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
}

bool TextRenderer::appendHyphenSplitIfFits(std::string& current,
                                           std::string_view word,
                                           const std::function<int(std::string_view)>& measureWidth,
                                           int maxWidth,
                                           std::vector<std::string>& out) {
    std::size_t split = word.rfind('-');
    while (split != std::string_view::npos && split > 0 && split < word.size() - 1) {
        const auto head = word.substr(0, split + 1);
        const auto tail = word.substr(split + 1);
        const std::string candidate = current + " " + std::string(head);
        if (measureWidth(candidate) <= maxWidth) {
            out.push_back(candidate);
            current = std::string(tail);
            return true;
        }
        split = word.rfind('-', split - 1);
    }
    return false;
}

} // namespace libreshockwave::player::render::output
