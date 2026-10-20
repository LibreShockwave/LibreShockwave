#include "libreshockwave/cast/xmed_text_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "libreshockwave/io/binary_reader.hpp"

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

int parseSectionLength(std::string_view ascii, std::size_t section_offset) {
    if (section_offset + 4 >= ascii.size()) {
        return 0;
    }
    const auto start = section_offset + 4;
    const auto end = std::min<std::size_t>(section_offset + 12, ascii.size());
    return parseHex(ascii.substr(start, end - start));
}

int parseSectionCount(std::string_view ascii, std::size_t section_offset) {
    if (section_offset + 12 >= ascii.size()) {
        return 0;
    }
    const auto start = section_offset + 12;
    const auto end = std::min<std::size_t>(section_offset + 20, ascii.size());
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

    std::vector<std::uint8_t> text_bytes(data.begin() + static_cast<std::ptrdiff_t>(comma + 1),
                                        data.begin() + static_cast<std::ptrdiff_t>(end));
    return macRomanString(text_bytes);
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
            const int name_len = data[i + 4] & 0xFF;
            if (name_len > 0 && name_len < 64 && i + 5 + static_cast<std::size_t>(name_len) <= data.size()) {
                std::string font(data.begin() + static_cast<std::ptrdiff_t>(i + 5),
                                 data.begin() + static_cast<std::ptrdiff_t>(i + 5 + name_len));
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
    const auto& mac_font = fonts.front();
    for (std::size_t i = 1; i < fonts.size(); ++i) {
        if (fonts[i] != mac_font) {
            return fonts[i];
        }
    }
    return mac_font;
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
    int font_candidate_index = -1;
    int font_size = -1;
};

std::vector<StyleRun> extractStyleRuns(const std::vector<std::uint8_t>& data,
                                       std::string_view ascii,
                                       int text_len);
std::vector<int> extractStyleRecordFontSizes(const std::vector<std::uint8_t>& data,
                                             std::size_t sec_start,
                                             std::size_t sec_end);
int chooseReferencedStyleFontSize(const std::vector<std::uint8_t>& data,
                                  std::string_view ascii,
                                  int text_len,
                                  const std::vector<int>& style_record_sizes);

std::array<int, 2> extractFontSizeAndStyle(const std::vector<std::uint8_t>& data,
                                           std::string_view ascii,
                                           int text_len) {
    const auto section = findSection(data, "0006");
    if (!section.has_value()) {
        return {9, 0};
    }

    const auto sec_start = *section + 20;
    const int sec_len = parseSectionLength(ascii, *section);
    const auto sec_end = sec_len > 0 ? std::min(sec_start + static_cast<std::size_t>(sec_len), data.size()) : data.size();
    const auto style_record_sizes = extractStyleRecordFontSizes(data, sec_start, sec_end);
    const int referenced_size = chooseReferencedStyleFontSize(data, ascii, text_len, style_record_sizes);
    if (referenced_size > 0) {
        return {referenced_size, 0};
    }

    std::vector<std::pair<int, int>> size_counts;
    int first_size = -1;
    for (std::size_t i = sec_start; i + 6 < sec_end; ++i) {
        if (data[i] != 0x02) {
            continue;
        }
        std::string hex;
        std::size_t j = i + 1;
        while (j < sec_end && isHexDigit(data[j])) {
            hex.push_back(static_cast<char>(data[j]));
            ++j;
        }
        if (hex.size() >= 5 && hex.ends_with("0000") && j < sec_end && data[j] == 0x02) {
            const int size = parseHex(std::string_view(hex).substr(0, hex.size() - 4));
            if (size >= 6 && size <= 200) {
                auto found = std::find_if(size_counts.begin(), size_counts.end(), [size](const auto& entry) {
                    return entry.first == size;
                });
                if (found == size_counts.end()) {
                    size_counts.emplace_back(size, 1);
                } else {
                    ++found->second;
                }
                if (first_size < 0) {
                    first_size = size;
                }
            }
        }
    }

    int font_size = 9;
    int max_count = 0;
    for (const auto& [size, count] : size_counts) {
        if (count > max_count || (count == max_count && size < font_size && font_size - size <= 2)) {
            font_size = size;
            max_count = count;
        }
    }
    if (size_counts.empty() && first_size > 0) {
        font_size = first_size;
    }
    return {font_size, 0};
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
    const auto capped_end = std::min(end, data.size());
    std::string hex;
    hex.reserve(capped_end - start);
    for (std::size_t i = start; i < capped_end; ++i) {
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
                                       int text_len) {
    const auto section = findSection(data, "0004");
    if (!section.has_value()) {
        return {};
    }

    const auto sec_start = *section + 20;
    const int sec_len = parseSectionLength(ascii, *section);
    const auto sec_end = sec_len > 0 ? std::min(sec_start + static_cast<std::size_t>(sec_len), data.size()) : data.size();
    std::vector<StyleRun> runs;
    std::size_t i = sec_start;
    while (i < sec_end) {
        if (data[i] != 0x02) {
            ++i;
            continue;
        }

        const auto offset_start = ++i;
        while (i < sec_end && isHexDigit(data[i])) {
            ++i;
        }
        const auto offset_end = i;
        if (offset_start == offset_end || i >= sec_end || data[i] != 0x01) {
            if (i < sec_end) ++i;
            continue;
        }

        const auto style_start = ++i;
        while (i < sec_end && isHexDigit(data[i])) {
            ++i;
        }
        if (style_start == i) {
            continue;
        }

        const int offset = parseHexField(data, offset_start, offset_end);
        const int style = parseHexField(data, style_start, i);
        if (offset >= 0 && offset <= text_len + 2) {
            runs.push_back(StyleRun{std::min(offset, text_len), style});
        }
    }

    std::sort(runs.begin(), runs.end(), [](const StyleRun& left, const StyleRun& right) {
        return left.offset < right.offset;
    });
    return runs;
}

std::vector<int> extractStyleRecordFontSizes(const std::vector<std::uint8_t>& data,
                                             std::size_t sec_start,
                                             std::size_t sec_end) {
    std::vector<int> sizes;
    for (std::size_t i = sec_start; i + 6 < sec_end; ++i) {
        if (data[i] != 0x02) {
            continue;
        }

        std::string hex;
        std::size_t j = i + 1;
        while (j < sec_end && isHexDigit(data[j])) {
            hex.push_back(static_cast<char>(data[j]));
            ++j;
        }
        if (hex.size() >= 5 && hex.ends_with("0000") && j < sec_end && data[j] == 0x02) {
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
                                  int text_len,
                                  const std::vector<int>& style_record_sizes) {
    if (style_record_sizes.empty() || text_len <= 0) {
        return -1;
    }

    const auto runs = extractStyleRuns(data, ascii, text_len);
    if (runs.empty()) {
        return -1;
    }

    std::vector<std::pair<int, int>> covered_chars_by_size;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (run.style < 0 || run.style >= static_cast<int>(style_record_sizes.size())) {
            continue;
        }
        const int start = std::clamp(run.offset, 0, text_len);
        int end = i + 1 < runs.size() ? runs[i + 1].offset : text_len;
        end = std::max(start, std::min(text_len, end));
        const int coverage = end - start;
        if (coverage <= 0) {
            continue;
        }
        const int size = style_record_sizes[static_cast<std::size_t>(run.style)];
        auto found = std::find_if(covered_chars_by_size.begin(), covered_chars_by_size.end(), [size](const auto& entry) {
            return entry.first == size;
        });
        if (found == covered_chars_by_size.end()) {
            covered_chars_by_size.emplace_back(size, coverage);
        } else {
            found->second += coverage;
        }
    }

    int best_size = -1;
    int best_coverage = 0;
    for (const auto& [size, coverage] : covered_chars_by_size) {
        if (coverage > best_coverage) {
            best_size = size;
            best_coverage = coverage;
        }
    }
    return best_size;
}

XmedStyleRecord parseStyleRecord(const std::vector<std::uint8_t>& record) {
    int font_candidate_index = -1;
    int font_size = -1;

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

        const auto field_start = pos + 1;
        const auto field_end = skipHexField(record, field_start);
        if (field_end == field_start) {
            ++pos;
            continue;
        }

        std::string raw;
        raw.reserve(field_end - field_start);
        for (auto i = field_start; i < field_end; ++i) {
            raw.push_back(static_cast<char>(record[i]));
        }
        const int value = parseHex(raw);
        if (marker == 0x01 && font_candidate_index < 0) {
            font_candidate_index = value;
        }
        if (marker == 0x02 && font_size < 0 && raw.size() >= 5 && raw.ends_with("0000")) {
            const int size = parseHex(std::string_view(raw).substr(0, raw.size() - 4));
            if (size >= 6 && size <= 200) {
                font_size = size;
            }
        }
        pos = field_end;
    }

    return XmedStyleRecord{font_candidate_index, font_size};
}

std::vector<XmedStyleRecord> extractStyleRecords(const std::vector<std::uint8_t>& data,
                                                 std::string_view ascii) {
    const auto section = findSection(data, "0006");
    if (!section.has_value()) {
        return {};
    }

    const auto sec_start = *section + 20;
    const int sec_len = parseSectionLength(ascii, *section);
    const auto sec_end = sec_len > 0 ? std::min(sec_start + static_cast<std::size_t>(sec_len), data.size()) : data.size();
    const int count = parseSectionCount(ascii, *section);
    if (count <= 0 || sec_start >= sec_end) {
        return {};
    }

    std::vector<std::uint8_t> body(data.begin() + static_cast<std::ptrdiff_t>(sec_start),
                                   data.begin() + static_cast<std::ptrdiff_t>(sec_end));
    std::vector<XmedStyleRecord> records;
    records.reserve(static_cast<std::size_t>(count));
    std::size_t record_start = 0;
    for (std::size_t i = 0; i + 1 < body.size() && static_cast<int>(records.size()) < count - 1; ++i) {
        if (body[i] == 0xC2 && body[i + 1] == 0x0A) {
            records.push_back(parseStyleRecord(std::vector<std::uint8_t>(
                body.begin() + static_cast<std::ptrdiff_t>(record_start),
                body.begin() + static_cast<std::ptrdiff_t>(i + 2))));
            record_start = i + 2;
        }
    }
    records.push_back(parseStyleRecord(std::vector<std::uint8_t>(
        body.begin() + static_cast<std::ptrdiff_t>(record_start),
        body.end())));
    return records;
}

const XmedStyleRecord* styleRecordForRun(const std::vector<XmedStyleRecord>& style_records, int run_style_index) {
    if (run_style_index < 0 || run_style_index >= static_cast<int>(style_records.size())) {
        return nullptr;
    }
    return &style_records[static_cast<std::size_t>(run_style_index)];
}

std::string resolveSpanFontName(const std::vector<std::string>& font_candidates,
                                const std::string& fallback_font_name,
                                const XmedStyleRecord* style_record) {
    if (style_record == nullptr || style_record->font_candidate_index < 0) {
        return fallback_font_name;
    }
    const auto index = static_cast<std::size_t>(style_record->font_candidate_index);
    if (index < font_candidates.size() && !font_candidates[index].empty()) {
        return font_candidates[index];
    }
    return fallback_font_name;
}

std::vector<StyledSpan> extractStyleSpans(const std::vector<std::uint8_t>& data,
                                          std::string_view ascii,
                                          int text_len,
                                          const std::vector<std::string>& font_candidates,
                                          const std::string& font_name,
                                          int font_size,
                                          bool bold,
                                          bool italic,
                                          const std::array<int, 3>& color) {
    const auto runs = extractStyleRuns(data, ascii, text_len);
    const auto style_records = extractStyleRecords(data, ascii);
    if (runs.empty()) {
        return {StyledSpan{0, text_len, font_name, font_size, bold, italic, false, color[0], color[1], color[2]}};
    }

    std::vector<StyledSpan> spans;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        const int start = std::clamp(run.offset, 0, text_len);
        int end = i + 1 < runs.size() ? runs[i + 1].offset : text_len;
        end = std::max(start, std::min(text_len, end));
        if (start == end) {
            continue;
        }

        const auto* style_record = styleRecordForRun(style_records, run.style);
        const auto span_font_name = resolveSpanFontName(font_candidates, font_name, style_record);
        const int span_font_size = style_record != nullptr && style_record->font_size > 0
            ? style_record->font_size
            : font_size;
        spans.push_back(StyledSpan{
            start,
            end,
            span_font_name,
            span_font_size,
            bold,
            italic,
            false,
            color[0],
            color[1],
            color[2]
        });
    }

    if (spans.empty()) {
        return {StyledSpan{0, text_len, font_name, font_size, bold, italic, false, color[0], color[1], color[2]}};
    }
    return spans;
}

std::string choosePrimarySpanFontName(const std::vector<StyledSpan>& spans, const std::string& fallback_font_name) {
    if (spans.empty()) {
        return fallback_font_name;
    }

    std::vector<std::pair<std::string, int>> coverage;
    for (const auto& span : spans) {
        const auto& span_font_name = span.font_name.empty() ? fallback_font_name : span.font_name;
        const int covered = std::max(0, span.end_offset - span.start_offset);
        auto found = std::find_if(coverage.begin(), coverage.end(), [&span_font_name](const auto& entry) {
            return entry.first == span_font_name;
        });
        if (found == coverage.end()) {
            coverage.emplace_back(span_font_name, covered);
        } else {
            found->second += covered;
        }
    }

    std::string best_font_name = fallback_font_name;
    int best_coverage = -1;
    for (const auto& [font_name, covered] : coverage) {
        if (covered > best_coverage) {
            best_font_name = font_name;
            best_coverage = covered;
        }
    }
    return best_font_name;
}

int choosePrimarySpanFontSize(const std::vector<StyledSpan>& spans, int fallback_font_size) {
    if (spans.empty()) {
        return fallback_font_size;
    }

    std::vector<std::pair<int, int>> coverage;
    for (const auto& span : spans) {
        const int covered = std::max(0, span.end_offset - span.start_offset);
        auto found = std::find_if(coverage.begin(), coverage.end(), [&span](const auto& entry) {
            return entry.first == span.font_size;
        });
        if (found == coverage.end()) {
            coverage.emplace_back(span.font_size, covered);
        } else {
            found->second += covered;
        }
    }

    int best_font_size = fallback_font_size;
    int best_coverage = -1;
    for (const auto& [font_size, covered] : coverage) {
        if (covered > best_coverage) {
            best_font_size = font_size;
            best_coverage = covered;
        }
    }
    return best_font_size;
}

int extractPrimaryParagraphStyleIndex(const std::vector<std::uint8_t>& data, std::string_view ascii) {
    const auto section = findSection(data, "0005");
    if (!section.has_value() || *section + 20 >= data.size()) {
        return 0;
    }
    const auto len_start = *section + 4;
    const auto len_end = std::min<std::size_t>(*section + 12, ascii.size());
    if (len_start >= len_end ||
        !std::all_of(ascii.begin() + static_cast<std::ptrdiff_t>(len_start),
                     ascii.begin() + static_cast<std::ptrdiff_t>(len_end),
                     [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
        return 0;
    }

    const auto body_start = *section + 20;
    std::size_t pos = body_start;
    if (pos < data.size() && data[pos] == 0x02) {
        ++pos;
    }
    for (std::size_t i = pos; i < std::min(body_start + 30, data.size()); ++i) {
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

    const auto sec_start = *section + 20;
    const int sec_len = parseSectionLength(ascii, *section);
    const auto sec_end = sec_len > 0 ? std::min(sec_start + static_cast<std::size_t>(sec_len), data.size()) : data.size();
    std::vector<std::vector<std::uint8_t>> records;
    auto start = sec_start;
    for (std::size_t i = sec_start; i + 1 < sec_end; ++i) {
        if (data[i] == 0xC2 && data[i + 1] == 0x12) {
            records.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(start),
                                 data.begin() + static_cast<std::ptrdiff_t>(i + 2));
            start = i + 2;
        }
    }
    return records;
}

int extractParagraphAlignmentCode(const std::vector<std::uint8_t>& paragraph_style_record) {
    if (paragraph_style_record.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i + 1 < paragraph_style_record.size(); ++i) {
        if (paragraph_style_record[i] != 0xC2 || paragraph_style_record[i + 1] != 0x0F) {
            continue;
        }
        for (std::size_t j = i + 2; j < paragraph_style_record.size(); ++j) {
            const int byte = paragraph_style_record[j] & 0xFF;
            if (byte == 0x01 || byte == 0x02) {
                return parseHexValue(paragraph_style_record, j + 1);
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

bool XmedTextParser::isTextXtra(const std::vector<std::uint8_t>& specific_data) {
    return specific_data.size() >= 8 &&
           specific_data[4] == 't' &&
           specific_data[5] == 'e' &&
           specific_data[6] == 'x' &&
           specific_data[7] == 't';
}

std::optional<XmedStyledText> XmedTextParser::parseStyled(const std::vector<std::uint8_t>& xmed_data,
                                                          const std::vector<std::uint8_t>& specific_data) {
    if (xmed_data.size() < 10) {
        return std::nullopt;
    }

    const std::string ascii = toAsciiString(xmed_data);
    auto text = extractText(xmed_data, ascii);
    if (!text.has_value()) {
        text = "";
    }
    auto font_candidates = extractFontNames(xmed_data, ascii);
    std::string font_name = selectPrimaryFontName(font_candidates);
    const int text_len = static_cast<int>(text->size());
    const auto [font_size, font_style] = extractFontSizeAndStyle(xmed_data, ascii, text_len);
    const auto color = extractColor(xmed_data, ascii);
    const int paragraph_style_index = extractPrimaryParagraphStyleIndex(xmed_data, ascii);
    const auto paragraph_style_records = extractParagraphStyleRecords(xmed_data, ascii);
    int paragraph_alignment_code = -1;
    if (paragraph_style_index >= 0 &&
        paragraph_style_index < static_cast<int>(paragraph_style_records.size())) {
        paragraph_alignment_code =
            extractParagraphAlignmentCode(paragraph_style_records[static_cast<std::size_t>(paragraph_style_index)]);
    }
    std::string alignment = alignmentFromParagraphAlignmentCode(paragraph_alignment_code);
    if (alignment.empty()) {
        alignment = alignmentFromParagraphStyleIndex(paragraph_style_index);
    }

    int width = 0;
    int height = 0;
    bool antialias = false;
    int aa_threshold = 14;
    if (specific_data.size() >= 56) {
        height = readU32BE(specific_data, 48);
        width = readU32BE(specific_data, 52);
    }
    if (specific_data.size() >= 40) {
        const int aa_disabled = readU32BE(specific_data, 12);
        if (aa_disabled == 0) {
            const int threshold_enabled = readU32BE(specific_data, 24);
            if (threshold_enabled == 0) {
                antialias = true;
            } else {
                aa_threshold = readU32BE(specific_data, 36);
                antialias = font_size >= aa_threshold;
            }
        }
    }

    const bool bold = (font_style & 1) != 0;
    const bool italic = (font_style & 2) != 0;
    std::vector<StyledSpan> spans = extractStyleSpans(xmed_data,
                                                      ascii,
                                                      text_len,
                                                      font_candidates,
                                                      font_name,
                                                      font_size,
                                                      bold,
                                                      italic,
                                                      color);
    const std::string primary_span_font_name = choosePrimarySpanFontName(spans, font_name);
    const int primary_span_font_size = choosePrimarySpanFontSize(spans, font_size);

    return XmedStyledText{
        *text,
        spans,
        font_candidates,
        alignment,
        paragraph_style_index,
        paragraph_alignment_code,
        static_cast<int>(paragraph_style_records.size()),
        true,
        0,
        width,
        height,
        primary_span_font_name,
        primary_span_font_size,
        antialias,
        aa_threshold,
        false,
        color[0],
        color[1],
        color[2]
    };
}

} // namespace libreshockwave::cast
