package com.libreshockwave.vm.util;

import com.libreshockwave.lingo.StringChunkType;

import java.util.ArrayList;
import java.util.List;

/**
 * Utilities for string chunk operations in Lingo.
 * Handles item/word/char/line extraction and counting.
 *
 * Uses a two-tier strategy to minimize GC pressure in WASM:
 * 1. Cached splits for long strings being iterated (avoids O(n^2) re-splitting)
 * 2. Direct scanning for short strings on cache miss (avoids List allocation)
 */
// Director treats all chars <= 0x20 (space) as word separators, including
// control characters like \x02 (used as Habbo protocol field delimiter).
public final class StringChunkUtils {

    private StringChunkUtils() {}

    /**
     * Strings shorter than this use direct scanning on cache miss (no List allocation).
     * For ITEM chunks, ALL strings use cursor-based scanning to avoid burst allocation
     * of thousands of substrings (the #1 cause of WASM heap corruption).
     */
    private static final int DIRECT_SCAN_THRESHOLD = 1024;

    // Per-type split caches (WASM is single-threaded, no synchronization needed).
    // Only used for WORD, CHAR, LINE on large strings.
    // ITEM chunks now use cursor-based scanning instead of split+cache.

    // WORD, CHAR, LINE: single entry each
    private static String _wordCacheStr; private static List<String> _wordResult;
    private static String _charCacheStr; private static List<String> _charResult;
    private static String _lineCacheStr; private static List<String> _lineResult;

    // === Two-slot LRU cursor for ITEM chunks ===
    // Avoids splitting large strings into thousands of substrings.
    // Two slots let us interleave item access on two strings (e.g., outer loop
    // iterates items of a large text, inner ops access items of each line).
    // Without two slots, inner operations invalidate the outer cursor → O(n²).
    private static String _seqStr0;
    private static char _seqDelim0;
    private static int _seqIdx0, _seqStartPos0, _seqEndPos0;

    private static String _seqStr1;
    private static char _seqDelim1;
    private static int _seqIdx1, _seqStartPos1, _seqEndPos1;

    /** Release cached split results to reduce GC pressure after heavy text processing. */
    public static void clearCaches() {
        _wordCacheStr = null; _wordResult = null;
        _charCacheStr = null; _charResult = null;
        _lineCacheStr = null; _lineResult = null;
        _seqStr0 = null; _seqStr1 = null;
    }

    /**
     * Get the last chunk of a string.
     */
    public static String getLastChunk(String str, StringChunkType chunkType, char itemDelimiter) {
        if (str == null || str.isEmpty()) return "";

        // ITEM: always use direct scan (no split allocation)
        if (chunkType == StringChunkType.ITEM) {
            int count = countDirect(str, chunkType, itemDelimiter);
            if (count == 0) return "";
            return getChunkDirect(str, chunkType, count, itemDelimiter);
        }

        // Other types: check cache first
        List<String> cached = getCachedChunks(str, chunkType, itemDelimiter);
        if (cached != null) return cached.isEmpty() ? "" : cached.get(cached.size() - 1);

        // Short string: direct scan
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            int count = countDirect(str, chunkType, itemDelimiter);
            if (count == 0) return "";
            return getChunkDirect(str, chunkType, count, itemDelimiter);
        }

        // Long string: split + cache
        List<String> chunks = splitIntoChunks(str, chunkType, itemDelimiter);
        return chunks.isEmpty() ? "" : chunks.get(chunks.size() - 1);
    }

    /**
     * Get a specific chunk by index (1-based).
     */
    public static String getChunk(String str, StringChunkType chunkType, int index, char itemDelimiter) {
        if (str == null || str.isEmpty() || index < 1) return "";

        // ITEM: always use cursor-based scan (no burst allocation)
        if (chunkType == StringChunkType.ITEM) {
            return getItemWithCursor(str, index, itemDelimiter);
        }

        // Other types: check cache first
        List<String> cached = getCachedChunks(str, chunkType, itemDelimiter);
        if (cached != null) return index > cached.size() ? "" : cached.get(index - 1);

        // Short string: direct scan (no List allocation)
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            return getChunkDirect(str, chunkType, index, itemDelimiter);
        }

        // Long string: split + cache
        List<String> chunks = splitIntoChunks(str, chunkType, itemDelimiter);
        return index > chunks.size() ? "" : chunks.get(index - 1);
    }

    /**
     * Get a range of chunks (1-based, inclusive).
     */
    public static String getChunkRange(String str, StringChunkType chunkType, int start, int end, char itemDelimiter) {
        if (str == null || str.isEmpty() || start < 1) return "";

        // ITEM: always use direct range scan (no burst allocation)
        if (chunkType == StringChunkType.ITEM) {
            int count = countDirect(str, chunkType, itemDelimiter);
            if (start > count) return "";
            int actualEnd = Math.min(end, count);
            if (start == actualEnd) return getItemWithCursor(str, start, itemDelimiter);
            return getItemRangeDirect(str, start, actualEnd, itemDelimiter);
        }

        // Other types: check cache first
        List<String> cached = getCachedChunks(str, chunkType, itemDelimiter);
        if (cached != null) {
            if (start > cached.size()) return "";
            int actualEnd = Math.min(end, cached.size());
            return String.join(getDelimiter(chunkType, itemDelimiter), cached.subList(start - 1, actualEnd));
        }

        // Short string: direct scan (no List allocation)
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            int count = countDirect(str, chunkType, itemDelimiter);
            if (start > count) return "";
            int actualEnd = Math.min(end, count);
            if (start == actualEnd) return getChunkDirect(str, chunkType, start, itemDelimiter);
            return getChunkRangeDirect(str, chunkType, start, actualEnd, itemDelimiter);
        }

        // Long string: split + cache
        List<String> chunks = splitIntoChunks(str, chunkType, itemDelimiter);
        if (start > chunks.size()) return "";
        int actualEnd = Math.min(end, chunks.size());
        return String.join(getDelimiter(chunkType, itemDelimiter), chunks.subList(start - 1, actualEnd));
    }

    /**
     * Count the number of chunks in a string.
     * Returns 1 for empty strings when chunk type is ITEM or LINE.
     */
    public static int countChunks(String str, StringChunkType chunkType, char itemDelimiter) {
        if (str == null || str.isEmpty()) {
            // Match dirplayer-rs: items and lines count as 1 even for empty strings
            if (chunkType == StringChunkType.ITEM || chunkType == StringChunkType.LINE) return 1;
            return 0;
        }

        // ITEM: always use direct count (no split allocation)
        if (chunkType == StringChunkType.ITEM) {
            return countDirect(str, chunkType, itemDelimiter);
        }

        // Other types: check cache first
        List<String> cached = getCachedChunks(str, chunkType, itemDelimiter);
        if (cached != null) return cached.size();

        // Short string: direct count (no List allocation)
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            return countDirect(str, chunkType, itemDelimiter);
        }

        // Long string: split + cache
        return splitIntoChunks(str, chunkType, itemDelimiter).size();
    }

    // ========================================================================
    // Cache lookup — returns cached result without populating on miss
    // ========================================================================

    private static List<String> getCachedChunks(String str, StringChunkType chunkType, char itemDelimiter) {
        // ITEM uses cursor-based scanning, not cache
        if (chunkType == StringChunkType.WORD) {
            return (str == _wordCacheStr && _wordResult != null) ? _wordResult : null;
        } else if (chunkType == StringChunkType.CHAR) {
            return (str == _charCacheStr && _charResult != null) ? _charResult : null;
        } else if (chunkType == StringChunkType.LINE) {
            return (str == _lineCacheStr && _lineResult != null) ? _lineResult : null;
        }
        return null;
    }

    // ========================================================================
    // Direct-scan methods — zero List/array allocation, O(n) per call.
    // Used for short strings on cache miss to reduce GC pressure.
    // ========================================================================

    private static int countDirect(String str, StringChunkType chunkType, char itemDelimiter) {
        if (chunkType == StringChunkType.CHAR) {
            return str.length();
        } else if (chunkType == StringChunkType.ITEM) {
            int count = 1;
            for (int i = 0; i < str.length(); i++) {
                if (str.charAt(i) == itemDelimiter) count++;
            }
            return count;
        } else if (chunkType == StringChunkType.WORD) {
            int count = 0;
            boolean inWord = false;
            for (int i = 0; i < str.length(); i++) {
                if (str.charAt(i) <= ' ') {
                    inWord = false;
                } else if (!inWord) {
                    count++;
                    inWord = true;
                }
            }
            return count;
        } else if (chunkType == StringChunkType.LINE) {
            String delim = pickLineDelimiter(str);
            int count = 1;
            int dLen = delim.length();
            int i = 0;
            while (i <= str.length() - dLen) {
                if (str.regionMatches(i, delim, 0, dLen)) {
                    count++;
                    i += dLen;
                } else {
                    i++;
                }
            }
            return count;
        }
        return 0;
    }

    private static String getChunkDirect(String str, StringChunkType chunkType, int index, char itemDelimiter) {
        if (chunkType == StringChunkType.CHAR) {
            return index <= str.length() ? String.valueOf(str.charAt(index - 1)) : "";
        } else if (chunkType == StringChunkType.ITEM) {
            int current = 1;
            int start = 0;
            for (int i = 0; i < str.length(); i++) {
                if (str.charAt(i) == itemDelimiter) {
                    if (current == index) return str.substring(start, i);
                    current++;
                    start = i + 1;
                }
            }
            return current == index ? str.substring(start) : "";
        } else if (chunkType == StringChunkType.WORD) {
            int wordNum = 0;
            int wordStart = 0;
            boolean inWord = false;
            for (int i = 0; i <= str.length(); i++) {
                boolean isSpace = i == str.length() || str.charAt(i) <= ' ';
                if (!isSpace && !inWord) {
                    wordNum++;
                    wordStart = i;
                    inWord = true;
                } else if (isSpace && inWord) {
                    if (wordNum == index) return str.substring(wordStart, i);
                    inWord = false;
                }
            }
            return "";
        } else if (chunkType == StringChunkType.LINE) {
            String delim = pickLineDelimiter(str);
            int dLen = delim.length();
            int lineNum = 1;
            int start = 0;
            int i = 0;
            while (i <= str.length() - dLen) {
                if (str.regionMatches(i, delim, 0, dLen)) {
                    if (lineNum == index) return str.substring(start, i);
                    lineNum++;
                    start = i + dLen;
                    i = start;
                } else {
                    i++;
                }
            }
            return lineNum == index ? str.substring(start) : "";
        }
        return "";
    }

    private static String getChunkRangeDirect(String str, StringChunkType chunkType, int startIdx, int endIdx, char itemDelimiter) {
        if (chunkType == StringChunkType.CHAR) {
            int s = Math.max(0, startIdx - 1);
            int e = Math.min(str.length(), endIdx);
            return (s < e) ? str.substring(s, e) : "";
        } else if (chunkType == StringChunkType.ITEM) {
            return getItemRangeDirect(str, startIdx, endIdx, itemDelimiter);
        } else if (chunkType == StringChunkType.WORD) {
            return getWordRangeDirect(str, startIdx, endIdx);
        } else if (chunkType == StringChunkType.LINE) {
            return getLineRangeDirect(str, startIdx, endIdx);
        }
        return "";
    }

    // ========================================================================
    // Two-slot LRU cursor for ITEM access — O(line_length) per sequential access.
    // Avoids splitting large strings into thousands of substrings.
    // Two slots handle interleaved iteration: outer loop on large text +
    // inner operations on each line. Without this, inner ops invalidate the
    // outer cursor, making tStr.item[i] O(n) per call → O(n²) total.
    // ========================================================================

    /**
     * Get item[index] using a two-slot LRU cursor. If accessing item[i+1] after item[i],
     * continues from the cached position (O(line_length) per call, O(n) total for iteration).
     * When a different string is accessed (e.g., inner loop), the outer cursor is preserved
     * in the second slot and restored when the outer string is accessed again.
     */
    private static String getItemWithCursor(String str, int index, char delimiter) {
        // Check slot 0 (MRU)
        if (str == _seqStr0 && delimiter == _seqDelim0) {
            if (index == _seqIdx0) {
                return str.substring(_seqStartPos0, _seqEndPos0);
            }
            if (index == _seqIdx0 + 1 && _seqEndPos0 < str.length()) {
                int start = _seqEndPos0 + 1;
                int end = indexOf(str, delimiter, start);
                _seqIdx0 = index;
                _seqStartPos0 = start;
                _seqEndPos0 = end;
                return str.substring(start, end);
            }
            // Same string but non-sequential access: rescan (stay in slot 0)
            return scanAndStore0(str, index, delimiter);
        }

        // Check slot 1 (LRU) — promote to slot 0 if found
        if (str == _seqStr1 && delimiter == _seqDelim1) {
            // Swap slot 1 → slot 0, old slot 0 → slot 1
            String tmpStr = _seqStr0; char tmpDelim = _seqDelim0;
            int tmpIdx = _seqIdx0, tmpStart = _seqStartPos0, tmpEnd = _seqEndPos0;
            _seqStr0 = _seqStr1; _seqDelim0 = _seqDelim1;
            _seqIdx0 = _seqIdx1; _seqStartPos0 = _seqStartPos1; _seqEndPos0 = _seqEndPos1;
            _seqStr1 = tmpStr; _seqDelim1 = tmpDelim;
            _seqIdx1 = tmpIdx; _seqStartPos1 = tmpStart; _seqEndPos1 = tmpEnd;

            if (index == _seqIdx0) {
                return str.substring(_seqStartPos0, _seqEndPos0);
            }
            if (index == _seqIdx0 + 1 && _seqEndPos0 < str.length()) {
                int start = _seqEndPos0 + 1;
                int end = indexOf(str, delimiter, start);
                _seqIdx0 = index;
                _seqStartPos0 = start;
                _seqEndPos0 = end;
                return str.substring(start, end);
            }
            return scanAndStore0(str, index, delimiter);
        }

        // New string: evict slot 1 (LRU), move slot 0 → slot 1, new string → slot 0
        _seqStr1 = _seqStr0; _seqDelim1 = _seqDelim0;
        _seqIdx1 = _seqIdx0; _seqStartPos1 = _seqStartPos0; _seqEndPos1 = _seqEndPos0;
        return scanAndStore0(str, index, delimiter);
    }

    /** Scan from beginning and store in slot 0. */
    private static String scanAndStore0(String str, int index, char delimiter) {
        _seqStr0 = str;
        _seqDelim0 = delimiter;
        int current = 1;
        int start = 0;
        for (int i = 0; i < str.length(); i++) {
            if (str.charAt(i) == delimiter) {
                if (current == index) {
                    _seqIdx0 = index;
                    _seqStartPos0 = start;
                    _seqEndPos0 = i;
                    return str.substring(start, i);
                }
                current++;
                start = i + 1;
            }
        }
        if (current == index) {
            _seqIdx0 = index;
            _seqStartPos0 = start;
            _seqEndPos0 = str.length();
            return str.substring(start);
        }
        return "";
    }

    /** Fast indexOf for a single char, returns str.length() if not found. */
    private static int indexOf(String str, char ch, int fromIndex) {
        for (int i = fromIndex; i < str.length(); i++) {
            if (str.charAt(i) == ch) return i;
        }
        return str.length();
    }

    /** Get items [startIdx..endIdx] joined by delimiter, using one-pass scan. */
    public static String getItemRangeDirect(String str, int startIdx, int endIdx, char delimiter) {
        int delimsSeen = 0;
        int segStart = (startIdx == 1) ? 0 : -1;

        for (int i = 0; i < str.length(); i++) {
            if (str.charAt(i) == delimiter) {
                delimsSeen++;
                if (startIdx > 1 && delimsSeen == startIdx - 1) segStart = i + 1;
                if (delimsSeen == endIdx) {
                    return (segStart >= 0) ? str.substring(segStart, i) : "";
                }
            }
        }
        return (segStart >= 0) ? str.substring(segStart) : "";
    }

    /** Get words [startIdx..endIdx] joined by space, normalizing whitespace. */
    public static String getWordRangeDirect(String str, int startIdx, int endIdx) {
        int wordNum = 0;
        int wordStart = 0;
        boolean inWord = false;
        StringBuilder sb = null;

        for (int i = 0; i <= str.length(); i++) {
            boolean isSpace = i == str.length() || str.charAt(i) <= ' ';
            if (!isSpace && !inWord) {
                wordNum++;
                wordStart = i;
                inWord = true;
            } else if (isSpace && inWord) {
                if (wordNum >= startIdx && wordNum <= endIdx) {
                    if (sb == null) sb = new StringBuilder();
                    else sb.append(' ');
                    sb.append(str, wordStart, i);
                }
                if (wordNum >= endIdx) break;
                inWord = false;
            }
        }
        return sb != null ? sb.toString() : "";
    }

    /** Get lines [startIdx..endIdx] joined by \r\n, using one-pass scan. */
    public static String getLineRangeDirect(String str, int startIdx, int endIdx) {
        String delim = pickLineDelimiter(str);
        int dLen = delim.length();
        int lineNum = 1;
        int segStart = (startIdx == 1) ? 0 : -1;
        int i = 0;
        while (i <= str.length() - dLen) {
            if (str.regionMatches(i, delim, 0, dLen)) {
                if (startIdx > 1 && lineNum == startIdx - 1) segStart = i + dLen;
                if (lineNum == endIdx) {
                    return (segStart >= 0) ? str.substring(segStart, i) : "";
                }
                lineNum++;
                i += dLen;
            } else {
                i++;
            }
        }
        return (segStart >= 0) ? str.substring(segStart) : "";
    }

    // ========================================================================
    // Full split + cache — for long strings being iterated
    // ========================================================================

    /**
     * Split a string into chunks based on chunk type.
     * Results are cached per type: repeated calls with the same string reference
     * return the cached result in O(1). ITEM type uses a two-entry LRU cache
     * to handle the common Lingo pattern of alternating delimiters within loops.
     */
    public static List<String> splitIntoChunks(String str, StringChunkType chunkType, char itemDelimiter) {
        if (str == null || str.isEmpty()) {
            return List.of();
        }

        // Per-type cache lookup
        // Use if-else to avoid TeaVM switch issues with enum types
        // ITEM uses cursor-based scanning, but splitIntoChunks may still be called
        // for non-performance-critical paths.
        if (chunkType == StringChunkType.ITEM) {
            return doSplit(str, chunkType, itemDelimiter);
        } else if (chunkType == StringChunkType.WORD) {
            if (str == _wordCacheStr && _wordResult != null) return _wordResult;
            List<String> result = doSplit(str, chunkType, itemDelimiter);
            _wordCacheStr = str; _wordResult = result;
            return result;
        } else if (chunkType == StringChunkType.CHAR) {
            if (str == _charCacheStr && _charResult != null) return _charResult;
            List<String> result = doSplit(str, chunkType, itemDelimiter);
            _charCacheStr = str; _charResult = result;
            return result;
        } else if (chunkType == StringChunkType.LINE) {
            if (str == _lineCacheStr && _lineResult != null) return _lineResult;
            List<String> result = doSplit(str, chunkType, itemDelimiter);
            _lineCacheStr = str; _lineResult = result;
            return result;
        } else {
            return doSplit(str, chunkType, itemDelimiter);
        }
    }

    /**
     * Perform the actual string split without caching.
     */
    private static List<String> doSplit(String str, StringChunkType chunkType, char itemDelimiter) {
        if (chunkType == StringChunkType.CHAR) {
            List<String> chars = new ArrayList<>(str.length());
            for (int i = 0; i < str.length(); i++) {
                chars.add(String.valueOf(str.charAt(i)));
            }
            return chars;
        } else if (chunkType == StringChunkType.WORD) {
            List<String> words = new ArrayList<>();
            StringBuilder current = new StringBuilder();
            for (int i = 0; i < str.length(); i++) {
                char c = str.charAt(i);
                if (c <= ' ') {
                    if (current.length() > 0) {
                        words.add(current.toString());
                        current.setLength(0);
                    }
                } else {
                    current.append(c);
                }
            }
            if (current.length() > 0) {
                words.add(current.toString());
            }
            return words;
        } else if (chunkType == StringChunkType.LINE) {
            String lineDelim = pickLineDelimiter(str);
            List<String> lines = new ArrayList<>();
            int start = 0;
            int delimLen = lineDelim.length();
            while (true) {
                int idx = str.indexOf(lineDelim, start);
                if (idx == -1) {
                    lines.add(str.substring(start));
                    break;
                }
                lines.add(str.substring(start, idx));
                start = idx + delimLen;
            }
            return lines;
        } else if (chunkType == StringChunkType.ITEM) {
            List<String> items = new ArrayList<>();
            StringBuilder current = new StringBuilder();
            for (int i = 0; i < str.length(); i++) {
                char c = str.charAt(i);
                if (c == itemDelimiter) {
                    items.add(current.toString());
                    current.setLength(0);
                } else {
                    current.append(c);
                }
            }
            items.add(current.toString());
            return items;
        }
        return List.of();
    }

    /**
     * Get the delimiter string for a chunk type.
     */
    private static String getDelimiter(StringChunkType chunkType, char itemDelimiter) {
        if (chunkType == StringChunkType.CHAR) return "";
        if (chunkType == StringChunkType.WORD) return " ";
        if (chunkType == StringChunkType.LINE) return "\r\n";
        if (chunkType == StringChunkType.ITEM) return String.valueOf(itemDelimiter);
        return "";
    }

    /**
     * Pick ONE line delimiter for the entire string, matching dirplayer-rs algorithm.
     * Check for \r\n first, then \n, then \r.
     */
    public static String pickLineDelimiter(String str) {
        if (str.contains("\r\n")) return "\r\n";
        if (str.contains("\n")) return "\n";
        if (str.contains("\r")) return "\r";
        return "\r\n"; // default
    }
}
