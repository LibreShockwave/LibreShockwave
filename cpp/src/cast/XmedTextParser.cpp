#include "libreshockwave/cast/XmedTextParser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::cast {
namespace {

bool isHexDigit(std::uint8_t byte) {
    return (byte >= '0' && byte <= '9') ||
           (byte >= 'A' && byte <= 'F') ||
           (byte >= 'a' && byte <= 'f');
}

std::string toAsciiString(const std::vector<std::uint8_t>& data) {
    std::string result;
    result.reserve(data.size());
    for (const auto byte : data) {
        result.push_back(byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.');
    }
    return result;
}

int parseHex(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    try {
        return std::stoi(std::string(text), nullptr, 16);
    } catch (const std::exception&) {
        return 0;
    }
}

int parseSectionLength(std::string_view ascii, std::size_t sectionOffset) {
    if (sectionOffset + 4 >= ascii.size()) {
        return 0;
    }
    const auto start = sectionOffset + 4;
    const auto end = std::min<std::size_t>(sectionOffset + 12, ascii.size());
    return parseHex(ascii.substr(start, end - start));
}

int parseSectionCount(std::string_view ascii, std::size_t sectionOffset) {
    if (sectionOffset + 12 >= ascii.size()) {
        return 0;
    }
    const auto start = sectionOffset + 12;
    const auto end = std::min<std::size_t>(sectionOffset + 20, ascii.size());
    return parseHex(ascii.substr(start, end - start));
}

std::optional<std::size_t> findSection(const std::vector<std::uint8_t>& data, std::string_view tag) {
    if (tag.empty() || data.size() < tag.size() + 1) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i + tag.size() < data.size(); ++i) {
        if (data[i] != 0x03) {
            continue;
        }
        bool match = true;
        for (std::size_t j = 0; j < tag.size(); ++j) {
            if (data[i + 1 + j] != static_cast<std::uint8_t>(tag[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return i + 1;
        }
    }
    return std::nullopt;
}

std::string macRomanString(const std::vector<std::uint8_t>& bytes) {
    io::BinaryReader reader(bytes, io::ByteOrder::BigEndian);
    return reader.readStringMacRoman(bytes.size());
}

std::optional<std::string> extractCountCommaText(const std::vector<std::uint8_t>& data, std::size_t pos) {
    if (pos >= data.size()) {
        return std::nullopt;
    }
    std::size_t end = data.size();
    for (std::size_t i = pos; i < data.size(); ++i) {
        if (data[i] == 0x03) {
            end = i;
            break;
        }
    }
    if (end <= pos) {
        return std::nullopt;
    }

    auto comma = end;
    for (std::size_t i = pos; i < end; ++i) {
        if (data[i] == ',') {
            comma = i;
            break;
        }
    }
    if (comma == end || comma + 1 >= end) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> textBytes(data.begin() + static_cast<std::ptrdiff_t>(comma + 1),
                                        data.begin() + static_cast<std::ptrdiff_t>(end));
    return macRomanString(textBytes);
}

std::optional<std::string> extractTextAfterTag(const std::vector<std::uint8_t>& data, std::size_t start) {
    for (std::size_t i = start; i + 2 < data.size(); ++i) {
        if (data[i] == 0x00) {
            return extractCountCommaText(data, i + 1);
        }
    }
    return std::nullopt;
}

bool tryParseTextAt(const std::vector<std::uint8_t>& data, std::size_t pos) {
    std::optional<std::size_t> comma;
    for (std::size_t i = pos; i < std::min(pos + 10, data.size()); ++i) {
        if (data[i] == ',') {
            comma = i;
            break;
        }
        if (!isHexDigit(data[i])) {
            return false;
        }
    }
    return comma.has_value() && *comma > pos && *comma + 1 < data.size() &&
           data[*comma + 1] >= 0x20 && data[*comma + 1] != 0x03;
}

std::optional<std::string> extractText(const std::vector<std::uint8_t>& data, std::string_view ascii) {
    const auto tag = ascii.find("0002");
    if (tag != std::string_view::npos) {
        return extractTextAfterTag(data, tag + 4);
    }

    for (std::size_t i = 0; i + 5 < data.size(); ++i) {
        if (data[i] == 0x00 && tryParseTextAt(data, i + 1)) {
            return extractCountCommaText(data, i + 1);
        }
    }
    return std::nullopt;
}

std::vector<std::string> extractFontNames(const std::vector<std::uint8_t>& data, std::string_view ascii) {
    const auto tag = ascii.find("0008");
    if (tag == std::string_view::npos) {
        return {};
    }

    std::vector<std::string> fonts;
    for (std::size_t i = tag + 20; i + 10 < data.size(); ++i) {
        if (data[i] == 0x03 && i + 3 < data.size() &&
            data[i + 1] == '0' && data[i + 2] == '0' && data[i + 3] == '0') {
            break;
        }
        if (data[i] == 0x00 && i + 4 < data.size() &&
            data[i + 1] == '4' && data[i + 2] == '0' && data[i + 3] == ',') {
            const int nameLen = data[i + 4] & 0xFF;
            if (nameLen > 0 && nameLen < 64 && i + 5 + static_cast<std::size_t>(nameLen) <= data.size()) {
                std::string font(data.begin() + static_cast<std::ptrdiff_t>(i + 5),
                                 data.begin() + static_cast<std::ptrdiff_t>(i + 5 + nameLen));
                while (!font.empty() && std::isspace(static_cast<unsigned char>(font.back()))) {
                    font.pop_back();
                }
                if (!font.empty() && std::find(fonts.begin(), fonts.end(), font) == fonts.end()) {
                    fonts.push_back(std::move(font));
                }
            }
            i += 4 + 64 - 1;
        }
    }
    return fonts;
}

std::string selectPrimaryFontName(const std::vector<std::string>& fonts) {
    if (fonts.empty()) {
        return "Geneva";
    }
    const auto& macFont = fonts.front();
    for (std::size_t i = 1; i < fonts.size(); ++i) {
        if (fonts[i] != macFont) {
            return fonts[i];
        }
    }
    return macFont;
}

std::array<int, 3> extractColor(const std::vector<std::uint8_t>& data, std::string_view ascii) {
    const auto tag = ascii.find("0003");
    if (tag != std::string_view::npos) {
        for (std::size_t i = tag + 4; i + 6 < data.size(); ++i) {
            if (data[i] != 0x00) {
                continue;
            }
            const auto text = extractCountCommaText(data, i + 1);
            if (!text.has_value()) {
                break;
            }
            std::array<int, 3> color{255, 255, 255};
            std::size_t start = 0;
            for (int component = 0; component < 3; ++component) {
                const auto comma = text->find(',', start);
                const auto part = text->substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                try {
                    color[static_cast<std::size_t>(component)] = std::stoi(part);
                } catch (const std::exception&) {
                    return {255, 255, 255};
                }
                if (comma == std::string::npos && component < 2) {
                    return {255, 255, 255};
                }
                start = comma == std::string::npos ? text->size() : comma + 1;
            }
            return color;
        }
    }
    return {255, 255, 255};
}

struct StyleRun {
    int offset = 0;
    int style = 0;
};

struct XmedStyleRecord {
    int fontCandidateIndex = -1;
    int fontSize = -1;
};

std::vector<StyleRun> extractStyleRuns(const std::vector<std::uint8_t>& data,
                                       std::string_view ascii,
                                       int textLen);
std::vector<int> extractStyleRecordFontSizes(const std::vector<std::uint8_t>& data,
                                             std::size_t secStart,
                                             std::size_t secEnd);
int chooseReferencedStyleFontSize(const std::vector<std::uint8_t>& data,
                                  std::string_view ascii,
                                  int textLen,
                                  const std::vector<int>& styleRecordSizes);

std::array<int, 2> extractFontSizeAndStyle(const std::vector<std::uint8_t>& data,
                                           std::string_view ascii,
                                           int textLen) {
    const auto section = findSection(data, "0006");
    if (!section.has_value()) {
        return {9, 0};
    }

    const auto secStart = *section + 20;
    const int secLen = parseSectionLength(ascii, *section);
    const auto secEnd = secLen > 0 ? std::min(secStart + static_cast<std::size_t>(secLen), data.size()) : data.size();
    const auto styleRecordSizes = extractStyleRecordFontSizes(data, secStart, secEnd);
    const int referencedSize = chooseReferencedStyleFontSize(data, ascii, textLen, styleRecordSizes);
    if (referencedSize > 0) {
        return {referencedSize, 0};
    }

    std::vector<std::pair<int, int>> sizeCounts;
    int firstSize = -1;
    for (std::size_t i = secStart; i + 6 < secEnd; ++i) {
        if (data[i] != 0x02) {
            continue;
        }
        std::string hex;
        std::size_t j = i + 1;
        while (j < secEnd && isHexDigit(data[j])) {
            hex.push_back(static_cast<char>(data[j]));
            ++j;
        }
        if (hex.size() >= 5 && hex.ends_with("0000") && j < secEnd && data[j] == 0x02) {
            const int size = parseHex(std::string_view(hex).substr(0, hex.size() - 4));
            if (size >= 6 && size <= 200) {
                auto found = std::find_if(sizeCounts.begin(), sizeCounts.end(), [size](const auto& entry) {
                    return entry.first == size;
                });
                if (found == sizeCounts.end()) {
                    sizeCounts.emplace_back(size, 1);
                } else {
                    ++found->second;
                }
                if (firstSize < 0) {
                    firstSize = size;
                }
            }
        }
    }

    int fontSize = 9;
    int maxCount = 0;
    for (const auto& [size, count] : sizeCounts) {
        if (count > maxCount || (count == maxCount && size < fontSize && fontSize - size <= 2)) {
            fontSize = size;
            maxCount = count;
        }
    }
    if (sizeCounts.empty() && firstSize > 0) {
        fontSize = firstSize;
    }
    return {fontSize, 0};
}

int parseHexValue(const std::vector<std::uint8_t>& data, std::size_t pos) {
    std::string hex;
    while (pos < data.size() && isHexDigit(data[pos])) {
        hex.push_back(static_cast<char>(data[pos]));
        ++pos;
    }
    return parseHex(hex);
}

int parseHexField(const std::vector<std::uint8_t>& data, std::size_t start, std::size_t end) {
    if (start >= end || start >= data.size()) {
        return 0;
    }
    const auto cappedEnd = std::min(end, data.size());
    std::string hex;
    hex.reserve(cappedEnd - start);
    for (std::size_t i = start; i < cappedEnd; ++i) {
        hex.push_back(static_cast<char>(data[i]));
    }
    return parseHex(hex);
}

std::size_t skipHexField(const std::vector<std::uint8_t>& data, std::size_t start) {
    auto pos = start;
    while (pos < data.size() && isHexDigit(data[pos])) {
        ++pos;
    }
    return pos;
}

std::vector<StyleRun> extractStyleRuns(const std::vector<std::uint8_t>& data,
                                       std::string_view ascii,
                                       int textLen) {
    const auto section = findSection(data, "0004");
    if (!section.has_value()) {
        return {};
    }

    const auto secStart = *section + 20;
    const int secLen = parseSectionLength(ascii, *section);
    const auto secEnd = secLen > 0 ? std::min(secStart + static_cast<std::size_t>(secLen), data.size()) : data.size();
    std::vector<StyleRun> runs;
    std::size_t i = secStart;
    while (i < secEnd) {
        if (data[i] != 0x02) {
            ++i;
            continue;
        }

        const auto offsetStart = ++i;
        while (i < secEnd && isHexDigit(data[i])) {
            ++i;
        }
        const auto offsetEnd = i;
        if (offsetStart == offsetEnd || i >= secEnd || data[i] != 0x01) {
            if (i < secEnd) ++i;
            continue;
        }

        const auto styleStart = ++i;
        while (i < secEnd && isHexDigit(data[i])) {
            ++i;
        }
        if (styleStart == i) {
            continue;
        }

        const int offset = parseHexField(data, offsetStart, offsetEnd);
        const int style = parseHexField(data, styleStart, i);
        if (offset >= 0 && offset <= textLen + 2) {
            runs.push_back(StyleRun{std::min(offset, textLen), style});
        }
    }

    std::sort(runs.begin(), runs.end(), [](const StyleRun& left, const StyleRun& right) {
        return left.offset < right.offset;
    });
    return runs;
}

std::vector<int> extractStyleRecordFontSizes(const std::vector<std::uint8_t>& data,
                                             std::size_t secStart,
                                             std::size_t secEnd) {
    std::vector<int> sizes;
    for (std::size_t i = secStart; i + 6 < secEnd; ++i) {
        if (data[i] != 0x02) {
            continue;
        }

        std::string hex;
        std::size_t j = i + 1;
        while (j < secEnd && isHexDigit(data[j])) {
            hex.push_back(static_cast<char>(data[j]));
            ++j;
        }
        if (hex.size() >= 5 && hex.ends_with("0000") && j < secEnd && data[j] == 0x02) {
            const int size = parseHex(std::string_view(hex).substr(0, hex.size() - 4));
            if (size >= 6 && size <= 200) {
                sizes.push_back(size);
            }
        }
    }
    return sizes;
}

int chooseReferencedStyleFontSize(const std::vector<std::uint8_t>& data,
                                  std::string_view ascii,
                                  int textLen,
                                  const std::vector<int>& styleRecordSizes) {
    if (styleRecordSizes.empty() || textLen <= 0) {
        return -1;
    }

    const auto runs = extractStyleRuns(data, ascii, textLen);
    if (runs.empty()) {
        return -1;
    }

    std::vector<std::pair<int, int>> coveredCharsBySize;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (run.style < 0 || run.style >= static_cast<int>(styleRecordSizes.size())) {
            continue;
        }
        const int start = std::clamp(run.offset, 0, textLen);
        int end = i + 1 < runs.size() ? runs[i + 1].offset : textLen;
        end = std::max(start, std::min(textLen, end));
        const int coverage = end - start;
        if (coverage <= 0) {
            continue;
        }
        const int size = styleRecordSizes[static_cast<std::size_t>(run.style)];
        auto found = std::find_if(coveredCharsBySize.begin(), coveredCharsBySize.end(), [size](const auto& entry) {
            return entry.first == size;
        });
        if (found == coveredCharsBySize.end()) {
            coveredCharsBySize.emplace_back(size, coverage);
        } else {
            found->second += coverage;
        }
    }

    int bestSize = -1;
    int bestCoverage = 0;
    for (const auto& [size, coverage] : coveredCharsBySize) {
        if (coverage > bestCoverage) {
            bestSize = size;
            bestCoverage = coverage;
        }
    }
    return bestSize;
}

XmedStyleRecord parseStyleRecord(const std::vector<std::uint8_t>& record) {
    int fontCandidateIndex = -1;
    int fontSize = -1;

    std::size_t pos = 0;
    if (!record.empty() && record[0] == 0x02) {
        pos = skipHexField(record, 1);
        if (pos < record.size() && record[pos] == 0x02) {
            pos = skipHexField(record, pos + 1);
        }
    }

    while (pos < record.size()) {
        const int marker = record[pos] & 0xFF;
        if (marker != 0x01 && marker != 0x02) {
            ++pos;
            continue;
        }

        const auto fieldStart = pos + 1;
        const auto fieldEnd = skipHexField(record, fieldStart);
        if (fieldEnd == fieldStart) {
            ++pos;
            continue;
        }

        std::string raw;
        raw.reserve(fieldEnd - fieldStart);
        for (auto i = fieldStart; i < fieldEnd; ++i) {
            raw.push_back(static_cast<char>(record[i]));
        }
        const int value = parseHex(raw);
        if (marker == 0x01 && fontCandidateIndex < 0) {
            fontCandidateIndex = value;
        }
        if (marker == 0x02 && fontSize < 0 && raw.size() >= 5 && raw.ends_with("0000")) {
            const int size = parseHex(std::string_view(raw).substr(0, raw.size() - 4));
            if (size >= 6 && size <= 200) {
                fontSize = size;
            }
        }
        pos = fieldEnd;
    }

    return XmedStyleRecord{fontCandidateIndex, fontSize};
}

std::vector<XmedStyleRecord> extractStyleRecords(const std::vector<std::uint8_t>& data,
                                                 std::string_view ascii) {
    const auto section = findSection(data, "0006");
    if (!section.has_value()) {
        return {};
    }

    const auto secStart = *section + 20;
    const int secLen = parseSectionLength(ascii, *section);
    const auto secEnd = secLen > 0 ? std::min(secStart + static_cast<std::size_t>(secLen), data.size()) : data.size();
    const int count = parseSectionCount(ascii, *section);
    if (count <= 0 || secStart >= secEnd) {
        return {};
    }

    std::vector<std::uint8_t> body(data.begin() + static_cast<std::ptrdiff_t>(secStart),
                                   data.begin() + static_cast<std::ptrdiff_t>(secEnd));
    std::vector<XmedStyleRecord> records;
    records.reserve(static_cast<std::size_t>(count));
    std::size_t recordStart = 0;
    for (std::size_t i = 0; i + 1 < body.size() && static_cast<int>(records.size()) < count - 1; ++i) {
        if (body[i] == 0xC2 && body[i + 1] == 0x0A) {
            records.push_back(parseStyleRecord(std::vector<std::uint8_t>(
                body.begin() + static_cast<std::ptrdiff_t>(recordStart),
                body.begin() + static_cast<std::ptrdiff_t>(i + 2))));
            recordStart = i + 2;
        }
    }
    records.push_back(parseStyleRecord(std::vector<std::uint8_t>(
        body.begin() + static_cast<std::ptrdiff_t>(recordStart),
        body.end())));
    return records;
}

const XmedStyleRecord* styleRecordForRun(const std::vector<XmedStyleRecord>& styleRecords, int runStyleIndex) {
    if (runStyleIndex < 0 || runStyleIndex >= static_cast<int>(styleRecords.size())) {
        return nullptr;
    }
    return &styleRecords[static_cast<std::size_t>(runStyleIndex)];
}

std::string resolveSpanFontName(const std::vector<std::string>& fontCandidates,
                                const std::string& fallbackFontName,
                                const XmedStyleRecord* styleRecord) {
    if (styleRecord == nullptr || styleRecord->fontCandidateIndex < 0) {
        return fallbackFontName;
    }
    const auto index = static_cast<std::size_t>(styleRecord->fontCandidateIndex);
    if (index < fontCandidates.size() && !fontCandidates[index].empty()) {
        return fontCandidates[index];
    }
    return fallbackFontName;
}

std::vector<StyledSpan> extractStyleSpans(const std::vector<std::uint8_t>& data,
                                          std::string_view ascii,
                                          int textLen,
                                          const std::vector<std::string>& fontCandidates,
                                          const std::string& fontName,
                                          int fontSize,
                                          bool bold,
                                          bool italic,
                                          const std::array<int, 3>& color) {
    const auto runs = extractStyleRuns(data, ascii, textLen);
    const auto styleRecords = extractStyleRecords(data, ascii);
    if (runs.empty()) {
        return {StyledSpan{0, textLen, fontName, fontSize, bold, italic, false, color[0], color[1], color[2]}};
    }

    std::vector<StyledSpan> spans;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        const int start = std::clamp(run.offset, 0, textLen);
        int end = i + 1 < runs.size() ? runs[i + 1].offset : textLen;
        end = std::max(start, std::min(textLen, end));
        if (start == end) {
            continue;
        }

        const auto* styleRecord = styleRecordForRun(styleRecords, run.style);
        const auto spanFontName = resolveSpanFontName(fontCandidates, fontName, styleRecord);
        const int spanFontSize = styleRecord != nullptr && styleRecord->fontSize > 0
            ? styleRecord->fontSize
            : fontSize;
        spans.push_back(StyledSpan{
            start,
            end,
            spanFontName,
            spanFontSize,
            bold,
            italic,
            (run.style & 1) != 0,
            color[0],
            color[1],
            color[2]
        });
    }

    if (spans.empty()) {
        return {StyledSpan{0, textLen, fontName, fontSize, bold, italic, false, color[0], color[1], color[2]}};
    }
    return spans;
}

std::string choosePrimarySpanFontName(const std::vector<StyledSpan>& spans, const std::string& fallbackFontName) {
    if (spans.empty()) {
        return fallbackFontName;
    }

    std::vector<std::pair<std::string, int>> coverage;
    for (const auto& span : spans) {
        const auto& spanFontName = span.fontName.empty() ? fallbackFontName : span.fontName;
        const int covered = std::max(0, span.endOffset - span.startOffset);
        auto found = std::find_if(coverage.begin(), coverage.end(), [&spanFontName](const auto& entry) {
            return entry.first == spanFontName;
        });
        if (found == coverage.end()) {
            coverage.emplace_back(spanFontName, covered);
        } else {
            found->second += covered;
        }
    }

    std::string bestFontName = fallbackFontName;
    int bestCoverage = -1;
    for (const auto& [fontName, covered] : coverage) {
        if (covered > bestCoverage) {
            bestFontName = fontName;
            bestCoverage = covered;
        }
    }
    return bestFontName;
}

int choosePrimarySpanFontSize(const std::vector<StyledSpan>& spans, int fallbackFontSize) {
    if (spans.empty()) {
        return fallbackFontSize;
    }

    std::vector<std::pair<int, int>> coverage;
    for (const auto& span : spans) {
        const int covered = std::max(0, span.endOffset - span.startOffset);
        auto found = std::find_if(coverage.begin(), coverage.end(), [&span](const auto& entry) {
            return entry.first == span.fontSize;
        });
        if (found == coverage.end()) {
            coverage.emplace_back(span.fontSize, covered);
        } else {
            found->second += covered;
        }
    }

    int bestFontSize = fallbackFontSize;
    int bestCoverage = -1;
    for (const auto& [fontSize, covered] : coverage) {
        if (covered > bestCoverage) {
            bestFontSize = fontSize;
            bestCoverage = covered;
        }
    }
    return bestFontSize;
}

int extractPrimaryParagraphStyleIndex(const std::vector<std::uint8_t>& data, std::string_view ascii) {
    const auto section = findSection(data, "0005");
    if (!section.has_value() || *section + 20 >= data.size()) {
        return 0;
    }
    const auto lenStart = *section + 4;
    const auto lenEnd = std::min<std::size_t>(*section + 12, ascii.size());
    if (lenStart >= lenEnd ||
        !std::all_of(ascii.begin() + static_cast<std::ptrdiff_t>(lenStart),
                     ascii.begin() + static_cast<std::ptrdiff_t>(lenEnd),
                     [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
        return 0;
    }

    const auto bodyStart = *section + 20;
    std::size_t pos = bodyStart;
    if (pos < data.size() && data[pos] == 0x02) {
        ++pos;
    }
    for (std::size_t i = pos; i < std::min(bodyStart + 30, data.size()); ++i) {
        if (data[i] == 0x01 && i + 1 < data.size()) {
            return parseHexValue(data, i + 1);
        }
    }
    return 0;
}

std::vector<std::vector<std::uint8_t>> extractParagraphStyleRecords(const std::vector<std::uint8_t>& data,
                                                                    std::string_view ascii) {
    const auto section = findSection(data, "0007");
    if (!section.has_value()) {
        return {};
    }

    const auto secStart = *section + 20;
    const int secLen = parseSectionLength(ascii, *section);
    const auto secEnd = secLen > 0 ? std::min(secStart + static_cast<std::size_t>(secLen), data.size()) : data.size();
    std::vector<std::vector<std::uint8_t>> records;
    auto start = secStart;
    for (std::size_t i = secStart; i + 1 < secEnd; ++i) {
        if (data[i] == 0xC2 && data[i + 1] == 0x12) {
            records.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(start),
                                 data.begin() + static_cast<std::ptrdiff_t>(i + 2));
            start = i + 2;
        }
    }
    return records;
}

int extractParagraphAlignmentCode(const std::vector<std::uint8_t>& paragraphStyleRecord) {
    if (paragraphStyleRecord.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i + 1 < paragraphStyleRecord.size(); ++i) {
        if (paragraphStyleRecord[i] != 0xC2 || paragraphStyleRecord[i + 1] != 0x0F) {
            continue;
        }
        for (std::size_t j = i + 2; j < paragraphStyleRecord.size(); ++j) {
            const int byte = paragraphStyleRecord[j] & 0xFF;
            if (byte == 0x01 || byte == 0x02) {
                return parseHexValue(paragraphStyleRecord, j + 1);
            }
        }
    }
    return -1;
}

std::string alignmentFromParagraphAlignmentCode(int value) {
    switch (value) {
        case 1: return "center";
        case 2: return "left";
        case 3: return "right";
        default: return "";
    }
}

std::string alignmentFromParagraphStyleIndex(int value) {
    switch (value) {
        case 1: return "center";
        case 2: return "right";
        default: return "left";
    }
}

int readU32BE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    const auto value = (static_cast<std::uint32_t>(data[offset]) << 24U) |
                       (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
                       (static_cast<std::uint32_t>(data[offset + 2]) << 8U) |
                       static_cast<std::uint32_t>(data[offset + 3]);
    return static_cast<int>(value);
}

} // namespace

bool XmedTextParser::isTextXtra(const std::vector<std::uint8_t>& specificData) {
    return specificData.size() >= 8 &&
           specificData[4] == 't' &&
           specificData[5] == 'e' &&
           specificData[6] == 'x' &&
           specificData[7] == 't';
}

std::optional<XmedStyledText> XmedTextParser::parseStyled(const std::vector<std::uint8_t>& xmedData,
                                                          const std::vector<std::uint8_t>& specificData) {
    if (xmedData.size() < 10) {
        return std::nullopt;
    }

    const std::string ascii = toAsciiString(xmedData);
    auto text = extractText(xmedData, ascii);
    if (!text.has_value()) {
        text = "";
    }
    auto fontCandidates = extractFontNames(xmedData, ascii);
    std::string fontName = selectPrimaryFontName(fontCandidates);
    const int textLen = static_cast<int>(text->size());
    const auto [fontSize, fontStyle] = extractFontSizeAndStyle(xmedData, ascii, textLen);
    const auto color = extractColor(xmedData, ascii);
    const int paragraphStyleIndex = extractPrimaryParagraphStyleIndex(xmedData, ascii);
    const auto paragraphStyleRecords = extractParagraphStyleRecords(xmedData, ascii);
    int paragraphAlignmentCode = -1;
    if (paragraphStyleIndex >= 0 &&
        paragraphStyleIndex < static_cast<int>(paragraphStyleRecords.size())) {
        paragraphAlignmentCode =
            extractParagraphAlignmentCode(paragraphStyleRecords[static_cast<std::size_t>(paragraphStyleIndex)]);
    }
    std::string alignment = alignmentFromParagraphAlignmentCode(paragraphAlignmentCode);
    if (alignment.empty()) {
        alignment = alignmentFromParagraphStyleIndex(paragraphStyleIndex);
    }

    int width = 0;
    int height = 0;
    bool antialias = false;
    int aaThreshold = 14;
    if (specificData.size() >= 56) {
        height = readU32BE(specificData, 48);
        width = readU32BE(specificData, 52);
    }
    if (specificData.size() >= 40) {
        const int aaDisabled = readU32BE(specificData, 12);
        if (aaDisabled == 0) {
            const int thresholdEnabled = readU32BE(specificData, 24);
            if (thresholdEnabled == 0) {
                antialias = true;
            } else {
                aaThreshold = readU32BE(specificData, 36);
                antialias = fontSize >= aaThreshold;
            }
        }
    }

    const bool bold = (fontStyle & 1) != 0;
    const bool italic = (fontStyle & 2) != 0;
    std::vector<StyledSpan> spans = extractStyleSpans(xmedData,
                                                      ascii,
                                                      textLen,
                                                      fontCandidates,
                                                      fontName,
                                                      fontSize,
                                                      bold,
                                                      italic,
                                                      color);
    const std::string primarySpanFontName = choosePrimarySpanFontName(spans, fontName);
    const int primarySpanFontSize = choosePrimarySpanFontSize(spans, fontSize);

    return XmedStyledText{
        *text,
        spans,
        fontCandidates,
        alignment,
        paragraphStyleIndex,
        paragraphAlignmentCode,
        static_cast<int>(paragraphStyleRecords.size()),
        true,
        0,
        width,
        height,
        primarySpanFontName,
        primarySpanFontSize,
        antialias,
        aaThreshold,
        false,
        color[0],
        color[1],
        color[2]
    };
}

} // namespace libreshockwave::cast
