#include "libreshockwave/player/render/output/text_renderer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace libreshockwave::player::render::output {

std::shared_ptr<bitmap::Bitmap> TextRenderer::renderXmedText(
    const ::libreshockwave::cast::XmedStyledText* styled_text,
    int width,
    int height,
    int text_color,
    int bg_color) {
    if (styled_text == nullptr) {
        return nullptr;
    }
    return renderText(styled_text->text,
                      width,
                      height,
                      styled_text->font_name,
                      styled_text->font_size,
                      styled_text->fontStyleString(),
                      styled_text->alignment,
                      text_color,
                      bg_color,
                      styled_text->word_wrap,
                      styled_text->antialias,
                      styled_text->fixed_line_space,
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

std::vector<int> TextRenderer::findCharLine(std::string_view text, int char_index) {
    if (text.empty()) {
        return {0, 0};
    }

    const int idx = std::max(0, std::min(char_index - 1, static_cast<int>(text.size())));
    int line_num = 0;
    int chars_on_line = 0;

    for (int pos = 0; pos < idx; ++pos) {
        const char ch = text[static_cast<std::size_t>(pos)];
        if (ch == '\r') {
            if ((pos + 1) < idx && (pos + 1) < static_cast<int>(text.size()) &&
                text[static_cast<std::size_t>(pos + 1)] == '\n') {
                ++pos;
            }
            ++line_num;
            chars_on_line = 0;
        } else if (ch == '\n') {
            ++line_num;
            chars_on_line = 0;
        } else {
            ++chars_on_line;
        }
    }

    return {line_num, chars_on_line};
}

int TextRenderer::lineStartIndex(std::string_view text, int target_line) {
    if (text.empty() || target_line <= 0) {
        return 0;
    }

    int line_num = 0;
    for (int pos = 0; pos < static_cast<int>(text.size()); ++pos) {
        const char ch = text[static_cast<std::size_t>(pos)];
        if (ch == '\r') {
            if ((pos + 1) < static_cast<int>(text.size()) && text[static_cast<std::size_t>(pos + 1)] == '\n') {
                ++pos;
            }
            ++line_num;
            if (line_num == target_line) {
                return pos + 1;
            }
        } else if (ch == '\n') {
            ++line_num;
            if (line_num == target_line) {
                return pos + 1;
            }
        }
    }
    return static_cast<int>(text.size());
}

void TextRenderer::wrapLine(std::string_view text,
                            const std::function<int(std::string_view)>& measure_width,
                            int max_width,
                            std::vector<std::string>& out) {
    if (text.empty()) {
        out.emplace_back("");
        return;
    }
    if (measure_width(text) <= max_width) {
        out.emplace_back(text);
        return;
    }

    std::istringstream words_in{std::string(text)};
    std::string word;
    std::string current;
    while (words_in >> word) {
        if (current.empty()) {
            current = word;
        } else {
            const std::string candidate = current + " " + word;
            if (measure_width(candidate) <= max_width) {
                current = candidate;
            } else if (appendHyphenSplitIfFits(current, word, measure_width, max_width, out)) {
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
                                           const std::function<int(std::string_view)>& measure_width,
                                           int max_width,
                                           std::vector<std::string>& out) {
    std::size_t split = word.rfind('-');
    while (split != std::string_view::npos && split > 0 && split < word.size() - 1) {
        const auto head = word.substr(0, split + 1);
        const auto tail = word.substr(split + 1);
        const std::string candidate = current + " " + std::string(head);
        if (measure_width(candidate) <= max_width) {
            out.push_back(candidate);
            current = std::string(tail);
            return true;
        }
        split = word.rfind('-', split - 1);
    }
    return false;
}

} // namespace libreshockwave::player::render::output
