#include "libreshockwave/lingo/vm/util/string_chunk_utils.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace libreshockwave::lingo::vm::util {
namespace {

std::vector<std::string> splitWords(std::string_view value) {
    std::vector<std::string> words;
    std::string current;
    for (char ch : value) {
        if (static_cast<unsigned char>(ch) <= static_cast<unsigned char>(' ')) {
            if (!current.empty()) {
                words.emplace_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        words.emplace_back(std::move(current));
    }
    return words;
}

std::vector<std::string> splitLines(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const std::string delimiter = pickLineDelimiter(value);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t found = value.find(delimiter, start);
        if (found == std::string_view::npos) {
            lines.emplace_back(value.substr(start));
            break;
        }
        lines.emplace_back(value.substr(start, found - start));
        start = found + delimiter.size();
    }
    return lines;
}

std::vector<std::string> splitItems(std::string_view value, char delimiter) {
    if (value.empty()) {
        return {};
    }

    std::vector<std::string> items;
    std::string current;
    for (char ch : value) {
        if (ch == delimiter) {
            items.emplace_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    items.emplace_back(std::move(current));
    return items;
}

std::vector<std::string> splitChars(std::string_view value) {
    std::vector<std::string> chars;
    chars.reserve(value.size());
    for (char ch : value) {
        chars.emplace_back(1, ch);
    }
    return chars;
}

int countItems(std::string_view value, char delimiter) {
    if (value.empty()) {
        return 1;
    }
    return static_cast<int>(std::count(value.begin(), value.end(), delimiter)) + 1;
}

int countDirect(std::string_view value, StringChunkType chunk_type, char item_delimiter) {
    switch (chunk_type) {
        case StringChunkType::Char:
            return static_cast<int>(value.size());
        case StringChunkType::Item:
            return countItems(value, item_delimiter);
        case StringChunkType::Word: {
            int count = 0;
            bool in_word = false;
            for (char ch : value) {
                if (static_cast<unsigned char>(ch) <= static_cast<unsigned char>(' ')) {
                    in_word = false;
                } else if (!in_word) {
                    ++count;
                    in_word = true;
                }
            }
            return count;
        }
        case StringChunkType::Line: {
            const std::string delimiter = pickLineDelimiter(value);
            int count = 1;
            std::size_t pos = 0;
            while (pos <= value.size()) {
                const std::size_t found = value.find(delimiter, pos);
                if (found == std::string_view::npos) {
                    break;
                }
                ++count;
                pos = found + delimiter.size();
            }
            return count;
        }
    }
    return 0;
}

std::string getChunkDirect(std::string_view value,
                           StringChunkType chunk_type,
                           int index,
                           char item_delimiter) {
    if (chunk_type == StringChunkType::Char) {
        return index <= static_cast<int>(value.size())
            ? std::string(1, value[static_cast<std::size_t>(index - 1)])
            : "";
    }
    if (chunk_type == StringChunkType::Item) {
        int current = 1;
        std::size_t start = 0;
        for (std::size_t pos = 0; pos < value.size(); ++pos) {
            if (value[pos] == item_delimiter) {
                if (current == index) {
                    return std::string(value.substr(start, pos - start));
                }
                ++current;
                start = pos + 1;
            }
        }
        return current == index ? std::string(value.substr(start)) : "";
    }
    if (chunk_type == StringChunkType::Word) {
        int word_num = 0;
        std::size_t word_start = 0;
        bool in_word = false;
        for (std::size_t pos = 0; pos <= value.size(); ++pos) {
            const bool is_space = pos == value.size() ||
                static_cast<unsigned char>(value[pos]) <= static_cast<unsigned char>(' ');
            if (!is_space && !in_word) {
                ++word_num;
                word_start = pos;
                in_word = true;
            } else if (is_space && in_word) {
                if (word_num == index) {
                    return std::string(value.substr(word_start, pos - word_start));
                }
                in_word = false;
            }
        }
        return "";
    }

    const std::string delimiter = pickLineDelimiter(value);
    int line_num = 1;
    std::size_t start = 0;
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t found = value.find(delimiter, pos);
        if (found == std::string_view::npos) {
            break;
        }
        if (line_num == index) {
            return std::string(value.substr(start, found - start));
        }
        ++line_num;
        start = found + delimiter.size();
        pos = start;
    }
    return line_num == index ? std::string(value.substr(start)) : "";
}

} // namespace

std::string pickLineDelimiter(std::string_view value) {
    if (value.find("\r\n") != std::string_view::npos) {
        return "\r\n";
    }
    if (value.find('\n') != std::string_view::npos) {
        return "\n";
    }
    if (value.find('\r') != std::string_view::npos) {
        return "\r";
    }
    return "\r\n";
}

std::string chunkDelimiter(StringChunkType chunk_type, char item_delimiter) {
    switch (chunk_type) {
        case StringChunkType::Char:
            return "";
        case StringChunkType::Word:
            return " ";
        case StringChunkType::Item:
            return std::string(1, item_delimiter);
        case StringChunkType::Line:
            return "\r\n";
    }
    return "";
}

std::vector<std::string> splitIntoChunks(std::string_view value,
                                         StringChunkType chunk_type,
                                         char item_delimiter) {
    switch (chunk_type) {
        case StringChunkType::Char:
            return splitChars(value);
        case StringChunkType::Word:
            return splitWords(value);
        case StringChunkType::Item:
            return splitItems(value, item_delimiter);
        case StringChunkType::Line:
            return splitLines(value);
    }
    return {};
}

int countChunks(std::string_view value, StringChunkType chunk_type, char item_delimiter) {
    if (value.empty()) {
        return chunk_type == StringChunkType::Item || chunk_type == StringChunkType::Line ? 1 : 0;
    }
    return countDirect(value, chunk_type, item_delimiter);
}

std::string getLastChunk(std::string_view value, StringChunkType chunk_type, char item_delimiter) {
    if (value.empty()) {
        return "";
    }
    const int count = countChunks(value, chunk_type, item_delimiter);
    return count == 0 ? std::string() : getChunk(value, chunk_type, count, item_delimiter);
}

std::string getChunk(std::string_view value, StringChunkType chunk_type, int index, char item_delimiter) {
    if (value.empty() || index < 1) {
        return "";
    }
    return getChunkDirect(value, chunk_type, index, item_delimiter);
}

std::string getChunkRange(std::string_view value,
                          StringChunkType chunk_type,
                          int start,
                          int end,
                          char item_delimiter) {
    if (value.empty() || start < 1) {
        return "";
    }
    if (end < 0) {
        end = countChunks(value, chunk_type, item_delimiter);
    } else if (end == 0) {
        end = start;
    }
    if (end < start) {
        return "";
    }
    if (start == end) {
        return getChunk(value, chunk_type, start, item_delimiter);
    }
    switch (chunk_type) {
        case StringChunkType::Char: {
            const int begin = std::max(0, start - 1);
            const int finish = std::min(static_cast<int>(value.size()), end);
            return begin < finish
                ? std::string(value.substr(static_cast<std::size_t>(begin),
                                           static_cast<std::size_t>(finish - begin)))
                : "";
        }
        case StringChunkType::Item:
            return getItemRangeDirect(value, start, end, item_delimiter);
        case StringChunkType::Word:
            return getWordRangeDirect(value, start, end);
        case StringChunkType::Line:
            return getLineRangeDirect(value, start, end);
    }
    return "";
}

std::string getItemRangeDirect(std::string_view value, int start, int end, char item_delimiter) {
    int delimiters_seen = 0;
    int segment_start = start == 1 ? 0 : -1;

    for (int pos = 0; pos < static_cast<int>(value.size()); ++pos) {
        if (value[static_cast<std::size_t>(pos)] == item_delimiter) {
            ++delimiters_seen;
            if (start > 1 && delimiters_seen == start - 1) {
                segment_start = pos + 1;
            }
            if (delimiters_seen == end) {
                return segment_start >= 0
                    ? std::string(value.substr(static_cast<std::size_t>(segment_start),
                                               static_cast<std::size_t>(pos - segment_start)))
                    : "";
            }
        }
    }
    return segment_start >= 0
        ? std::string(value.substr(static_cast<std::size_t>(segment_start)))
        : "";
}

std::string getWordRangeDirect(std::string_view value, int start, int end) {
    int word_num = 0;
    std::size_t word_start = 0;
    bool in_word = false;
    std::string result;
    result.reserve(value.size());

    for (std::size_t pos = 0; pos <= value.size(); ++pos) {
        const bool is_space = pos == value.size() ||
            static_cast<unsigned char>(value[pos]) <= static_cast<unsigned char>(' ');
        if (!is_space && !in_word) {
            ++word_num;
            word_start = pos;
            in_word = true;
        } else if (is_space && in_word) {
            if (word_num >= start && word_num <= end) {
                if (!result.empty()) {
                    result.push_back(' ');
                }
                result += value.substr(word_start, pos - word_start);
            }
            if (word_num >= end) {
                break;
            }
            in_word = false;
        }
    }
    return result;
}

std::string getLineRangeDirect(std::string_view value, int start, int end) {
    const std::string delimiter = pickLineDelimiter(value);
    int line_num = 1;
    int segment_start = start == 1 ? 0 : -1;
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t found = value.find(delimiter, pos);
        if (found == std::string_view::npos) {
            break;
        }
        if (start > 1 && line_num == start - 1) {
            segment_start = static_cast<int>(found + delimiter.size());
        }
        if (line_num == end) {
            return segment_start >= 0
                ? std::string(value.substr(static_cast<std::size_t>(segment_start),
                                           found - static_cast<std::size_t>(segment_start)))
                : "";
        }
        ++line_num;
        pos = found + delimiter.size();
    }
    return segment_start >= 0
        ? std::string(value.substr(static_cast<std::size_t>(segment_start)))
        : "";
}

LineIndex buildLineIndex(std::string_view value) {
    LineIndex index;
    index.delimiter = pickLineDelimiter(value);
    index.source_size = value.size();
    index.source_hash = std::hash<std::string_view>{}(value);
    index.starts.push_back(0);

    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t found = value.find(index.delimiter, start);
        if (found == std::string_view::npos) {
            index.ends.push_back(value.size());
            break;
        }
        index.ends.push_back(found);
        start = found + index.delimiter.size();
        index.starts.push_back(start);
    }
    return index;
}

int lineCount(const LineIndex& index) {
    return static_cast<int>(index.starts.size());
}

std::string getLineRange(std::string_view value, const LineIndex& index, int start, int end) {
    if (value.empty() || start < 1) {
        return "";
    }
    if (end == 0) {
        end = start;
    }
    if (index.source_size != value.size() ||
        index.source_hash != std::hash<std::string_view>{}(value)) {
        return getLineRangeDirect(value, start, end);
    }
    if (end < 0) {
        end = lineCount(index);
    }
    if (end < start) {
        return "";
    }

    const int clamped_end = std::min(end, lineCount(index));
    if (start > clamped_end) {
        return "";
    }
    if (index.starts.size() != index.ends.size()) {
        return getLineRangeDirect(value, start, end);
    }

    const std::size_t range_start = index.starts[static_cast<std::size_t>(start - 1)];
    const std::size_t range_end = index.ends[static_cast<std::size_t>(clamped_end - 1)];
    if (range_start > value.size() || range_end > value.size()) {
        return getLineRangeDirect(value, start, end);
    }
    return range_end >= range_start
        ? std::string(value.substr(range_start, range_end - range_start))
        : "";
}

} // namespace libreshockwave::lingo::vm::util
