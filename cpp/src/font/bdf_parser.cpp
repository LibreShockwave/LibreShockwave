#include "libreshockwave/font/bdf_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libreshockwave::font {
namespace {

struct GlyphData {
    int encoding = -1;
    int dwidth = 0;
    int bbx_w = 0;
    int bbx_h = 0;
    int bbx_x = 0;
    int bbx_y = 0;
    std::vector<std::uint64_t> rows;
    int row_byte_width = 0;
};

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    if (first > 0) {
        value.erase(0, first);
    }
    return value;
}

int parseIntAfter(std::string_view line, std::string_view prefix) {
    return std::stoi(std::string(line.substr(prefix.size())));
}

GlyphData parseGlyph(std::istream& in) {
    GlyphData glyph;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (startsWith(line, "ENCODING ")) {
            glyph.encoding = parseIntAfter(line, "ENCODING ");
        } else if (startsWith(line, "DWIDTH ")) {
            std::istringstream parts{line.substr(7)};
            parts >> glyph.dwidth;
        } else if (startsWith(line, "BBX ")) {
            std::istringstream parts{line.substr(4)};
            parts >> glyph.bbx_w >> glyph.bbx_h >> glyph.bbx_x >> glyph.bbx_y;
        } else if (line == "BITMAP") {
            glyph.rows.assign(static_cast<std::size_t>(std::max(0, glyph.bbx_h)), 0);
            glyph.row_byte_width = (glyph.bbx_w + 7) / 8;
            for (int i = 0; i < glyph.bbx_h;) {
                std::string hex_line;
                if (!std::getline(in, hex_line)) {
                    break;
                }
                hex_line = trim(std::move(hex_line));
                if (hex_line.empty()) {
                    continue;
                }
                glyph.rows[static_cast<std::size_t>(i)] = std::stoull(hex_line, nullptr, 16);
                ++i;
            }
        } else if (line == "ENDCHAR") {
            break;
        }
    }
    return glyph;
}

void blitGlyphToBuffer(const GlyphData& glyph,
                       std::vector<std::uint32_t>& argb,
                       int bitmap_width,
                       int bitmap_height,
                       int dst_x,
                       int dst_y) {
    for (int by = 0; by < glyph.bbx_h && by < static_cast<int>(glyph.rows.size()); ++by) {
        const int py = dst_y + by;
        if (py < 0 || py >= bitmap_height) {
            continue;
        }
        const auto row_bits = glyph.rows[static_cast<std::size_t>(by)];
        for (int bx = 0; bx < glyph.bbx_w; ++bx) {
            const int px = dst_x + bx;
            if (px < 0 || px >= bitmap_width) {
                continue;
            }
            const int bit_pos = (glyph.row_byte_width * 8 - 1) - bx;
            if (bit_pos < 0) {
                continue;
            }
            if ((row_bits & (1ULL << static_cast<unsigned>(bit_pos))) != 0) {
                argb[static_cast<std::size_t>(py * bitmap_width + px)] = 0xFF000000U;
            }
        }
    }
}

void blitGlyph(const GlyphData& glyph,
               std::vector<std::uint32_t>& argb,
               int bitmap_width,
               int bitmap_height,
               int cell_width,
               int cell_height,
               int char_code,
               int font_ascent) {
    const int col = char_code % BitmapFont::GRID_COLUMNS;
    const int row = char_code / BitmapFont::GRID_COLUMNS;
    const int cell_x = col * cell_width;
    const int cell_y = row * cell_height;
    const int glyph_top = cell_y + (font_ascent - glyph.bbx_y - glyph.bbx_h);
    const int glyph_left = cell_x + glyph.bbx_x;
    blitGlyphToBuffer(glyph, argb, bitmap_width, bitmap_height, glyph_left, glyph_top);
}

void blitGlyphToCell(const GlyphData& glyph,
                     std::vector<std::uint32_t>& cell_buf,
                     int cell_width,
                     int cell_height,
                     int font_ascent) {
    const int glyph_top = font_ascent - glyph.bbx_y - glyph.bbx_h;
    const int glyph_left = glyph.bbx_x;
    blitGlyphToBuffer(glyph, cell_buf, cell_width, cell_height, glyph_left, glyph_top);
}

std::shared_ptr<BitmapFont> doParse(std::istream& in, std::string font_name) {
    int font_ascent = 0;
    int font_descent = 0;
    int pixel_size = 0;
    std::unordered_map<int, GlyphData> glyphs;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (startsWith(line, "FONT_ASCENT ")) {
            font_ascent = parseIntAfter(line, "FONT_ASCENT ");
        } else if (startsWith(line, "FONT_DESCENT ")) {
            font_descent = parseIntAfter(line, "FONT_DESCENT ");
        } else if (startsWith(line, "PIXEL_SIZE ")) {
            pixel_size = parseIntAfter(line, "PIXEL_SIZE ");
        } else if (startsWith(line, "STARTCHAR ")) {
            auto glyph = parseGlyph(in);
            if (glyph.encoding >= 0) {
                glyphs[glyph.encoding] = std::move(glyph);
            }
        }
    }

    if (glyphs.empty()) {
        return nullptr;
    }

    int total_height = font_ascent + font_descent;
    if (total_height <= 0) {
        total_height = pixel_size > 0 ? pixel_size : 12;
    }
    if (font_ascent <= 0) {
        font_ascent = total_height;
    }

    int max_cell_w = 1;
    for (const auto& [_, glyph] : glyphs) {
        const int needed = std::max(glyph.dwidth, glyph.bbx_w + std::max(0, glyph.bbx_x));
        max_cell_w = std::max(max_cell_w, needed);
    }

    const int cell_width = max_cell_w;
    const int cell_height = total_height;
    const int bitmap_width = cell_width * BitmapFont::GRID_COLUMNS;
    const int bitmap_height = cell_height * BitmapFont::GRID_ROWS;
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmap_width * bitmap_height), 0);
    std::vector<int> char_widths(BitmapFont::NUM_CHARS, cell_width / 2);
    BitmapFont::GlyphMap overflow_glyphs;
    BitmapFont::MetricMap overflow_widths;

    for (const auto& [char_code, glyph] : glyphs) {
        if (char_code < BitmapFont::NUM_CHARS) {
            char_widths[static_cast<std::size_t>(char_code)] = glyph.dwidth;
            blitGlyph(glyph, argb, bitmap_width, bitmap_height, cell_width, cell_height, char_code, font_ascent);
        } else if (char_code < 256) {
            overflow_widths[char_code] = glyph.dwidth;
            std::vector<std::uint32_t> cell_buf(static_cast<std::size_t>(cell_width * cell_height), 0);
            blitGlyphToCell(glyph, cell_buf, cell_width, cell_height, font_ascent);
            overflow_glyphs[char_code] = std::move(cell_buf);
        }
    }

    const int font_size = pixel_size > 0 ? pixel_size : total_height;
    return BitmapFont::create(std::move(argb),
                              bitmap_width,
                              bitmap_height,
                              cell_width,
                              cell_height,
                              std::move(char_widths),
                              std::move(font_name),
                              font_size,
                              font_ascent,
                              total_height,
                              std::move(overflow_glyphs),
                              std::move(overflow_widths));
}

} // namespace

std::shared_ptr<BitmapFont> BdfParser::parse(std::string_view bdf_text, std::string font_name) {
    try {
        std::istringstream in{std::string(bdf_text)};
        return doParse(in, std::move(font_name));
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::shared_ptr<BitmapFont> BdfParser::parse(const std::vector<std::uint8_t>& bytes, std::string font_name) {
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), std::move(font_name));
}

} // namespace libreshockwave::font
