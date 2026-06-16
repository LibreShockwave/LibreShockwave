#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace libreshockwave::font {

class Pfr1Font {
public:
    struct CharacterRecord {
        int charCode = 0;
        int setWidth = 0;
        int gpsSize = 0;
        int gpsOffset = 0;
    };

    struct FontMetrics {
        int outlineResolution = 2048;
        int metricsResolution = 2048;
        int xMin = 0;
        int yMin = 0;
        int xMax = 0;
        int yMax = 0;
        int ascender = 0;
        int descender = 0;
        int stdVW = 0;
        int stdHW = 0;
        bool hasBitmapSection = false;
        std::string fontId;
    };

    struct Contour {
        struct Command {
            int type = 0;
            float x = 0.0F;
            float y = 0.0F;
            float x1 = 0.0F;
            float y1 = 0.0F;
            float x2 = 0.0F;
            float y2 = 0.0F;
        };

        std::vector<Command> commands;
        void moveTo(float x, float y);
        void lineTo(float x, float y);
        void curveTo(float x1, float y1, float x2, float y2, float x, float y);
    };

    struct OutlineGlyph {
        int charCode = 0;
        float setWidth = 0.0F;
        std::vector<Contour> contours;
    };

    struct BitmapGlyph {
        int charCode = 0;
        int imageFormat = 0;
        int xPos = 0;
        int yPos = 0;
        int xSize = 0;
        int ySize = 0;
        int setWidth = 0;
        std::vector<std::uint8_t> imageData;
    };

    [[nodiscard]] static std::shared_ptr<Pfr1Font> parse(const std::vector<std::uint8_t>& data);

    std::string fontName;
    FontMetrics metrics;
    std::vector<CharacterRecord> charRecords;
    std::unordered_map<int, OutlineGlyph> glyphs;
    std::unordered_map<int, BitmapGlyph> bitmapGlyphs;
    std::array<int, 4> fontMatrix{256, 0, 0, 256};
    bool pfrBlackPixel = false;
    int gpsOffset = 0;
    int gpsSize = 0;

private:
    void parseHeader(const std::vector<std::uint8_t>& data);
    void parseLogicalFontDirectory(const std::vector<std::uint8_t>& data,
                                   int dirSize,
                                   int dirOffset,
                                   int sectionSize,
                                   int sectionOffset);
    void parsePhysicalFont(const std::vector<std::uint8_t>& data);
    void parseDeltaEncodedCharRecords(class PfrBitReader& reader,
                                      int nCharacters,
                                      int standardSetWidth);
    void parseGlyphStubsAndBitmaps(const std::vector<std::uint8_t>& data);
    [[nodiscard]] BitmapGlyph parseBitmapGlyph(const std::vector<std::uint8_t>& data,
                                               int start,
                                               int size,
                                               int charCode) const;

    int storedMaxChars_ = 0;
};

} // namespace libreshockwave::font
