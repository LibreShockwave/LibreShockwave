#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::vm::util {

struct LineIndex {
    std::string delimiter;
    std::size_t sourceSize = 0;
    std::size_t sourceHash = 0;
    std::vector<std::size_t> starts;
    std::vector<std::size_t> ends;
};

[[nodiscard]] std::string pickLineDelimiter(std::string_view value);
[[nodiscard]] std::string chunkDelimiter(StringChunkType chunkType, char itemDelimiter = ',');
[[nodiscard]] std::vector<std::string> splitIntoChunks(std::string_view value,
                                                       StringChunkType chunkType,
                                                       char itemDelimiter = ',');
[[nodiscard]] int countChunks(std::string_view value, StringChunkType chunkType, char itemDelimiter = ',');
[[nodiscard]] std::string getLastChunk(std::string_view value,
                                       StringChunkType chunkType,
                                       char itemDelimiter = ',');
[[nodiscard]] std::string getChunk(std::string_view value,
                                   StringChunkType chunkType,
                                   int index,
                                   char itemDelimiter = ',');
[[nodiscard]] std::string getChunkRange(std::string_view value,
                                        StringChunkType chunkType,
                                        int start,
                                        int end,
                                        char itemDelimiter = ',');
[[nodiscard]] std::string getItemRangeDirect(std::string_view value,
                                             int start,
                                             int end,
                                             char itemDelimiter = ',');
[[nodiscard]] std::string getWordRangeDirect(std::string_view value, int start, int end);
[[nodiscard]] std::string getLineRangeDirect(std::string_view value, int start, int end);
[[nodiscard]] LineIndex buildLineIndex(std::string_view value);
[[nodiscard]] int lineCount(const LineIndex& index);
[[nodiscard]] std::string getLineRange(std::string_view value, const LineIndex& index, int start, int end);

} // namespace libreshockwave::lingo::vm::util
