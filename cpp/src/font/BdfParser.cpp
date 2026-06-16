#include "libreshockwave/font/BdfParser.hpp"

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
    int bbxW = 0;
    int bbxH = 0;
    int bbxX = 0;
    int bbxY = 0;
    std::vector<std::uint64_t> rows;
    int rowByteWidth = 0;
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
            parts >> glyph.bbxW >> glyph.bbxH >> glyph.bbxX >> glyph.bbxY;
        } else if (line == "BITMAP") {
            glyph.rows.assign(static_cast<std::size_t>(std::max(0, glyph.bbxH)), 0);
            glyph.rowByteWidth = (glyph.bbxW + 7) / 8;
            for (int i = 0; i < glyph.bbxH;) {
                std::string hexLine;
                if (!std::getline(in, hexLine)) {
                    break;
                }
                hexLine = trim(std::move(hexLine));
                if (hexLine.empty()) {
                    continue;
                }
                glyph.rows[static_cast<std::size_t>(i)] = std::stoull(hexLine, nullptr, 16);
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
                       int bitmapWidth,
                       int bitmapHeight,
                       int dstX,
                       int dstY) {
    for (int by = 0; by < glyph.bbxH && by < static_cast<int>(glyph.rows.size()); ++by) {
        const int py = dstY + by;
        if (py < 0 || py >= bitmapHeight) {
            continue;
        }
        const auto rowBits = glyph.rows[static_cast<std::size_t>(by)];
        for (int bx = 0; bx < glyph.bbxW; ++bx) {
            const int px = dstX + bx;
            if (px < 0 || px >= bitmapWidth) {
                continue;
            }
            const int bitPos = (glyph.rowByteWidth * 8 - 1) - bx;
            if (bitPos < 0) {
                continue;
            }
            if ((rowBits & (1ULL << static_cast<unsigned>(bitPos))) != 0) {
                argb[static_cast<std::size_t>(py * bitmapWidth + px)] = 0xFF000000U;
            }
        }
    }
}

void blitGlyph(const GlyphData& glyph,
               std::vector<std::uint32_t>& argb,
               int bitmapWidth,
               int bitmapHeight,
               int cellWidth,
               int cellHeight,
               int charCode,
               int fontAscent) {
    const int col = charCode % BitmapFont::GRID_COLUMNS;
    const int row = charCode / BitmapFont::GRID_COLUMNS;
    const int cellX = col * cellWidth;
    const int cellY = row * cellHeight;
    const int glyphTop = cellY + (fontAscent - glyph.bbxY - glyph.bbxH);
    const int glyphLeft = cellX + glyph.bbxX;
    blitGlyphToBuffer(glyph, argb, bitmapWidth, bitmapHeight, glyphLeft, glyphTop);
}

void blitGlyphToCell(const GlyphData& glyph,
                     std::vector<std::uint32_t>& cellBuf,
                     int cellWidth,
                     int cellHeight,
                     int fontAscent) {
    const int glyphTop = fontAscent - glyph.bbxY - glyph.bbxH;
    const int glyphLeft = glyph.bbxX;
    blitGlyphToBuffer(glyph, cellBuf, cellWidth, cellHeight, glyphLeft, glyphTop);
}

std::shared_ptr<BitmapFont> doParse(std::istream& in, std::string fontName) {
    int fontAscent = 0;
    int fontDescent = 0;
    int pixelSize = 0;
    std::unordered_map<int, GlyphData> glyphs;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (startsWith(line, "FONT_ASCENT ")) {
            fontAscent = parseIntAfter(line, "FONT_ASCENT ");
        } else if (startsWith(line, "FONT_DESCENT ")) {
            fontDescent = parseIntAfter(line, "FONT_DESCENT ");
        } else if (startsWith(line, "PIXEL_SIZE ")) {
            pixelSize = parseIntAfter(line, "PIXEL_SIZE ");
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

    int totalHeight = fontAscent + fontDescent;
    if (totalHeight <= 0) {
        totalHeight = pixelSize > 0 ? pixelSize : 12;
    }
    if (fontAscent <= 0) {
        fontAscent = totalHeight;
    }

    int maxCellW = 1;
    for (const auto& [_, glyph] : glyphs) {
        const int needed = std::max(glyph.dwidth, glyph.bbxW + std::max(0, glyph.bbxX));
        maxCellW = std::max(maxCellW, needed);
    }

    const int cellWidth = maxCellW;
    const int cellHeight = totalHeight;
    const int bitmapWidth = cellWidth * BitmapFont::GRID_COLUMNS;
    const int bitmapHeight = cellHeight * BitmapFont::GRID_ROWS;
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmapWidth * bitmapHeight), 0);
    std::vector<int> charWidths(BitmapFont::NUM_CHARS, cellWidth / 2);
    BitmapFont::GlyphMap overflowGlyphs;
    BitmapFont::MetricMap overflowWidths;

    for (const auto& [charCode, glyph] : glyphs) {
        if (charCode < BitmapFont::NUM_CHARS) {
            charWidths[static_cast<std::size_t>(charCode)] = glyph.dwidth;
            blitGlyph(glyph, argb, bitmapWidth, bitmapHeight, cellWidth, cellHeight, charCode, fontAscent);
        } else if (charCode < 256) {
            overflowWidths[charCode] = glyph.dwidth;
            std::vector<std::uint32_t> cellBuf(static_cast<std::size_t>(cellWidth * cellHeight), 0);
            blitGlyphToCell(glyph, cellBuf, cellWidth, cellHeight, fontAscent);
            overflowGlyphs[charCode] = std::move(cellBuf);
        }
    }

    const int fontSize = pixelSize > 0 ? pixelSize : totalHeight;
    return BitmapFont::create(std::move(argb),
                              bitmapWidth,
                              bitmapHeight,
                              cellWidth,
                              cellHeight,
                              std::move(charWidths),
                              std::move(fontName),
                              fontSize,
                              fontAscent,
                              totalHeight,
                              std::move(overflowGlyphs),
                              std::move(overflowWidths));
}

} // namespace

std::shared_ptr<BitmapFont> BdfParser::parse(std::string_view bdfText, std::string fontName) {
    try {
        std::istringstream in{std::string(bdfText)};
        return doParse(in, std::move(fontName));
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::shared_ptr<BitmapFont> BdfParser::parse(const std::vector<std::uint8_t>& bytes, std::string fontName) {
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), std::move(fontName));
}

} // namespace libreshockwave::font
