#include "libreshockwave/lingo/vm/util/StringChunkUtils.hpp"

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

int countDirect(std::string_view value, StringChunkType chunkType, char itemDelimiter) {
    switch (chunkType) {
        case StringChunkType::Char:
            return static_cast<int>(value.size());
        case StringChunkType::Item:
            return countItems(value, itemDelimiter);
        case StringChunkType::Word: {
            int count = 0;
            bool inWord = false;
            for (char ch : value) {
                if (static_cast<unsigned char>(ch) <= static_cast<unsigned char>(' ')) {
                    inWord = false;
                } else if (!inWord) {
                    ++count;
                    inWord = true;
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
                           StringChunkType chunkType,
                           int index,
                           char itemDelimiter) {
    if (chunkType == StringChunkType::Char) {
        return index <= static_cast<int>(value.size())
            ? std::string(1, value[static_cast<std::size_t>(index - 1)])
            : "";
    }
    if (chunkType == StringChunkType::Item) {
        int current = 1;
        std::size_t start = 0;
        for (std::size_t pos = 0; pos < value.size(); ++pos) {
            if (value[pos] == itemDelimiter) {
                if (current == index) {
                    return std::string(value.substr(start, pos - start));
                }
                ++current;
                start = pos + 1;
            }
        }
        return current == index ? std::string(value.substr(start)) : "";
    }
    if (chunkType == StringChunkType::Word) {
        int wordNum = 0;
        std::size_t wordStart = 0;
        bool inWord = false;
        for (std::size_t pos = 0; pos <= value.size(); ++pos) {
            const bool isSpace = pos == value.size() ||
                static_cast<unsigned char>(value[pos]) <= static_cast<unsigned char>(' ');
            if (!isSpace && !inWord) {
                ++wordNum;
                wordStart = pos;
                inWord = true;
            } else if (isSpace && inWord) {
                if (wordNum == index) {
                    return std::string(value.substr(wordStart, pos - wordStart));
                }
                inWord = false;
            }
        }
        return "";
    }

    const std::string delimiter = pickLineDelimiter(value);
    int lineNum = 1;
    std::size_t start = 0;
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t found = value.find(delimiter, pos);
        if (found == std::string_view::npos) {
            break;
        }
        if (lineNum == index) {
            return std::string(value.substr(start, found - start));
        }
        ++lineNum;
        start = found + delimiter.size();
        pos = start;
    }
    return lineNum == index ? std::string(value.substr(start)) : "";
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

std::string chunkDelimiter(StringChunkType chunkType, char itemDelimiter) {
    switch (chunkType) {
        case StringChunkType::Char:
            return "";
        case StringChunkType::Word:
            return " ";
        case StringChunkType::Item:
            return std::string(1, itemDelimiter);
        case StringChunkType::Line:
            return "\r\n";
    }
    return "";
}

std::vector<std::string> splitIntoChunks(std::string_view value,
                                         StringChunkType chunkType,
                                         char itemDelimiter) {
    switch (chunkType) {
        case StringChunkType::Char:
            return splitChars(value);
        case StringChunkType::Word:
            return splitWords(value);
        case StringChunkType::Item:
            return splitItems(value, itemDelimiter);
        case StringChunkType::Line:
            return splitLines(value);
    }
    return {};
}

int countChunks(std::string_view value, StringChunkType chunkType, char itemDelimiter) {
    if (value.empty()) {
        return chunkType == StringChunkType::Item || chunkType == StringChunkType::Line ? 1 : 0;
    }
    return countDirect(value, chunkType, itemDelimiter);
}

std::string getLastChunk(std::string_view value, StringChunkType chunkType, char itemDelimiter) {
    if (value.empty()) {
        return "";
    }
    const int count = countChunks(value, chunkType, itemDelimiter);
    return count == 0 ? std::string() : getChunk(value, chunkType, count, itemDelimiter);
}

std::string getChunk(std::string_view value, StringChunkType chunkType, int index, char itemDelimiter) {
    if (value.empty() || index < 1) {
        return "";
    }
    return getChunkDirect(value, chunkType, index, itemDelimiter);
}

std::string getChunkRange(std::string_view value,
                          StringChunkType chunkType,
                          int start,
                          int end,
                          char itemDelimiter) {
    if (value.empty() || start < 1) {
        return "";
    }
    if (end < 0) {
        end = countChunks(value, chunkType, itemDelimiter);
    } else if (end == 0) {
        end = start;
    }
    if (end < start) {
        return "";
    }
    if (start == end) {
        return getChunk(value, chunkType, start, itemDelimiter);
    }
    switch (chunkType) {
        case StringChunkType::Char: {
            const int begin = std::max(0, start - 1);
            const int finish = std::min(static_cast<int>(value.size()), end);
            return begin < finish
                ? std::string(value.substr(static_cast<std::size_t>(begin),
                                           static_cast<std::size_t>(finish - begin)))
                : "";
        }
        case StringChunkType::Item:
            return getItemRangeDirect(value, start, end, itemDelimiter);
        case StringChunkType::Word:
            return getWordRangeDirect(value, start, end);
        case StringChunkType::Line:
            return getLineRangeDirect(value, start, end);
    }
    return "";
}

std::string getItemRangeDirect(std::string_view value, int start, int end, char itemDelimiter) {
    int delimitersSeen = 0;
    int segmentStart = start == 1 ? 0 : -1;

    for (int pos = 0; pos < static_cast<int>(value.size()); ++pos) {
        if (value[static_cast<std::size_t>(pos)] == itemDelimiter) {
            ++delimitersSeen;
            if (start > 1 && delimitersSeen == start - 1) {
                segmentStart = pos + 1;
            }
            if (delimitersSeen == end) {
                return segmentStart >= 0
                    ? std::string(value.substr(static_cast<std::size_t>(segmentStart),
                                               static_cast<std::size_t>(pos - segmentStart)))
                    : "";
            }
        }
    }
    return segmentStart >= 0
        ? std::string(value.substr(static_cast<std::size_t>(segmentStart)))
        : "";
}

std::string getWordRangeDirect(std::string_view value, int start, int end) {
    int wordNum = 0;
    std::size_t wordStart = 0;
    bool inWord = false;
    std::string result;
    result.reserve(value.size());

    for (std::size_t pos = 0; pos <= value.size(); ++pos) {
        const bool isSpace = pos == value.size() ||
            static_cast<unsigned char>(value[pos]) <= static_cast<unsigned char>(' ');
        if (!isSpace && !inWord) {
            ++wordNum;
            wordStart = pos;
            inWord = true;
        } else if (isSpace && inWord) {
            if (wordNum >= start && wordNum <= end) {
                if (!result.empty()) {
                    result.push_back(' ');
                }
                result += value.substr(wordStart, pos - wordStart);
            }
            if (wordNum >= end) {
                break;
            }
            inWord = false;
        }
    }
    return result;
}

std::string getLineRangeDirect(std::string_view value, int start, int end) {
    const std::string delimiter = pickLineDelimiter(value);
    int lineNum = 1;
    int segmentStart = start == 1 ? 0 : -1;
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t found = value.find(delimiter, pos);
        if (found == std::string_view::npos) {
            break;
        }
        if (start > 1 && lineNum == start - 1) {
            segmentStart = static_cast<int>(found + delimiter.size());
        }
        if (lineNum == end) {
            return segmentStart >= 0
                ? std::string(value.substr(static_cast<std::size_t>(segmentStart),
                                           found - static_cast<std::size_t>(segmentStart)))
                : "";
        }
        ++lineNum;
        pos = found + delimiter.size();
    }
    return segmentStart >= 0
        ? std::string(value.substr(static_cast<std::size_t>(segmentStart)))
        : "";
}

LineIndex buildLineIndex(std::string_view value) {
    LineIndex index;
    index.delimiter = pickLineDelimiter(value);
    index.sourceSize = value.size();
    index.sourceHash = std::hash<std::string_view>{}(value);
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
    if (index.sourceSize != value.size() ||
        index.sourceHash != std::hash<std::string_view>{}(value)) {
        return getLineRangeDirect(value, start, end);
    }
    if (end < 0) {
        end = lineCount(index);
    }
    if (end < start) {
        return "";
    }

    const int clampedEnd = std::min(end, lineCount(index));
    if (start > clampedEnd) {
        return "";
    }
    if (index.starts.size() != index.ends.size()) {
        return getLineRangeDirect(value, start, end);
    }

    const std::size_t rangeStart = index.starts[static_cast<std::size_t>(start - 1)];
    const std::size_t rangeEnd = index.ends[static_cast<std::size_t>(clampedEnd - 1)];
    if (rangeStart > value.size() || rangeEnd > value.size()) {
        return getLineRangeDirect(value, start, end);
    }
    return rangeEnd >= rangeStart
        ? std::string(value.substr(rangeStart, rangeEnd - rangeStart))
        : "";
}

} // namespace libreshockwave::lingo::vm::util
