package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.util.StringChunkUtils;

import java.util.ArrayList;
import java.util.List;

/**
 * Handles method calls on string values.
 * Supports Lingo string chunk operations like count, getProp, getPropRef.
 *
 * Uses a two-tier strategy to minimize GC pressure in WASM:
 * 1. Cached splits for long strings being iterated (avoids O(n^2) re-splitting)
 * 2. Direct scanning for short strings on cache miss (avoids array allocation)
 */
public final class StringMethodDispatcher {

    private StringMethodDispatcher() {}

    /** Strings shorter than this use direct scanning on cache miss (no array allocation). */
    private static final int DIRECT_SCAN_THRESHOLD = 1024;

    // Per-type split caches (WASM is single-threaded).
    // ITEM uses direct scanning always (no split+cache) to avoid burst allocations.

    // WORD, LINE: single entry each
    private static String _wordCacheStr;
    private static String[] _wordCacheResult;

    private static String _lineCacheStr;
    private static String[] _lineCacheResult;

    /** Release cached split results to reduce GC pressure after heavy text processing. */
    public static void clearCaches() {
        _wordCacheStr = null; _wordCacheResult = null;
        _lineCacheStr = null; _lineCacheResult = null;
    }

    public static Datum dispatch(Datum str, String methodName, List<Datum> args) {
        // Use equalsIgnoreCase to avoid toLowerCase allocation
        char itemDelimiter = getItemDelimiter();
        String value = str.toStr();

        if ("length".equalsIgnoreCase(methodName)) {
            return Datum.of(value.length());
        } else if ("char".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.EMPTY_STRING;
            int index = args.get(0).toInt();
            if (index >= 1 && index <= value.length()) {
                return Datum.of(String.valueOf(value.charAt(index - 1)));
            }
            return Datum.EMPTY_STRING;
        } else if ("count".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.of(value.length());
            Datum chunkType = args.get(0);
            if (chunkType instanceof Datum.Symbol s) {
                String type = s.name();
                if ("char".equalsIgnoreCase(type)) return Datum.of(value.length());
                else if ("word".equalsIgnoreCase(type)) return Datum.of(countWords(value));
                else if ("line".equalsIgnoreCase(type)) return Datum.of(countLines(value));
                else if ("item".equalsIgnoreCase(type)) return Datum.of(countItems(value, itemDelimiter));
                else return Datum.of(value.length());
            }
            return Datum.of(value.length());
        } else if ("getpropref".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.EMPTY_STRING;
            Datum chunkType = args.get(0);
            int index = args.get(1).toInt();
            if (!(chunkType instanceof Datum.Symbol s)) return Datum.EMPTY_STRING;
            return Datum.of(getStringChunk(value, s.name(), index, index, itemDelimiter));
        } else if ("getprop".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.EMPTY_STRING;
            Datum chunkType = args.get(0);
            int startIndex = args.get(1).toInt();
            int endIndex = args.size() >= 3 ? args.get(2).toInt() : startIndex;
            if (!(chunkType instanceof Datum.Symbol s)) return Datum.EMPTY_STRING;
            String result = getStringChunk(value, s.name(), startIndex, endIndex, itemDelimiter);
            return Datum.of(result);
        }
        return Datum.VOID;
    }

    /**
     * Get the current item delimiter from MoviePropertyProvider.
     */
    private static char getItemDelimiter() {
        return MoviePropertyProvider.ItemDelimiterCache._char;
    }

    /**
     * Get a chunk from a string.
     * Handles edge cases: end == 0 means single element (set end = start),
     * end == -1 means to-end (set end = chunk count).
     * Uses cached splits for long iterated strings, direct scanning for short strings.
     */
    private static String getStringChunk(String str, String chunkType, int start, int end, char itemDelimiter) {
        if (str.isEmpty() || start < 1) return "";

        // Use equalsIgnoreCase to avoid toLowerCase allocation
        if ("char".equalsIgnoreCase(chunkType)) {
            int actualEnd = resolveEnd(end, start, str.length());
            int s = Math.max(0, start - 1);
            int e = Math.min(str.length(), actualEnd);
            if (s >= str.length() || s >= e) return "";
            return str.substring(s, e);
        } else if ("word".equalsIgnoreCase(chunkType)) {
            return getWordChunk(str, start, end);
        } else if ("line".equalsIgnoreCase(chunkType)) {
            return getLineChunk(str, start, end);
        } else if ("item".equalsIgnoreCase(chunkType)) {
            return getItemChunk(str, start, end, itemDelimiter);
        }
        return "";
    }

    // ========================================================================
    // ITEM chunk access — cache-first, then direct scan for short strings
    // ========================================================================

    private static String getItemChunk(String str, int start, int end, char delimiter) {
        // Always use direct scan for ITEM to avoid burst allocation of thousands of substrings
        int totalItems = countItemsDirect(str, delimiter);
        int actualEnd = resolveEnd(end, start, totalItems);
        if (start > totalItems) return "";
        int e = Math.min(totalItems, actualEnd);
        if (start == e) return getItemDirect(str, start, delimiter);
        return StringChunkUtils.getItemRangeDirect(str, start, e, delimiter);
    }

    // extractFromArray removed — ITEM chunks no longer use split+cache

    // ========================================================================
    // WORD chunk access — cache-first, then direct scan for short strings
    // ========================================================================

    private static String getWordChunk(String str, int start, int end) {
        // Check cache
        if (str == _wordCacheStr && _wordCacheResult != null) {
            return extractWordsFromArray(_wordCacheResult, start, end);
        }
        // Cache miss — direct scan for short strings
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            int totalWords = countWordsDirect(str);
            if (totalWords == 0 || start > totalWords) return "";
            int actualEnd = resolveEnd(end, start, totalWords);
            int e = Math.min(totalWords, actualEnd);
            return StringChunkUtils.getWordRangeDirect(str, start, e);
        }
        // Long string — split + cache
        return extractWordsFromArray(cachedSplitWords(str), start, end);
    }

    private static String extractWordsFromArray(String[] words, int start, int end) {
        return extractChunkRange(words, start, end, " ");
    }

    // ========================================================================
    // LINE chunk access — cache-first, then direct scan for short strings
    // ========================================================================

    private static String getLineChunk(String str, int start, int end) {
        // Check cache
        if (str == _lineCacheStr && _lineCacheResult != null) {
            return extractLinesFromArray(_lineCacheResult, start, end);
        }
        // Cache miss — direct scan for short strings
        if (str.length() < DIRECT_SCAN_THRESHOLD) {
            int totalLines = countLinesDirect(str);
            if (start > totalLines) return "";
            int actualEnd = resolveEnd(end, start, totalLines);
            int e = Math.min(totalLines, actualEnd);
            return StringChunkUtils.getLineRangeDirect(str, start, e);
        }
        // Long string — split + cache
        return extractLinesFromArray(cachedSplitLines(str), start, end);
    }

    private static String extractLinesFromArray(String[] lines, int start, int end) {
        return extractChunkRange(lines, start, end, "\r\n");
    }

    // ========================================================================
    // Count methods — cache-first, then direct count for short strings
    // ========================================================================

    private static int countWords(String str) {
        if (str.isEmpty()) return 0;
        if (str == _wordCacheStr && _wordCacheResult != null) return _wordCacheResult.length;
        if (str.length() < DIRECT_SCAN_THRESHOLD) return countWordsDirect(str);
        return cachedSplitWords(str).length;
    }

    private static int countLines(String str) {
        if (str.isEmpty()) return 1;
        if (str == _lineCacheStr && _lineCacheResult != null) return _lineCacheResult.length;
        if (str.length() < DIRECT_SCAN_THRESHOLD) return countLinesDirect(str);
        return cachedSplitLines(str).length;
    }

    private static int countItems(String str, char delimiter) {
        if (str.isEmpty()) return 1;
        // Always use direct count for ITEM (no split allocation)
        return countItemsDirect(str, delimiter);
    }

    // ========================================================================
    // Resolve end parameter
    // ========================================================================

    private static int resolveEnd(int end, int start, int count) {
        if (end == 0) return start;
        if (end == -1) return count;
        return end;
    }

    private static String extractChunkRange(String[] chunks, int start, int end, String separator) {
        int actualEnd = resolveEnd(end, start, chunks.length);
        if (start > chunks.length) return "";
        int s = start - 1;
        int e = Math.min(chunks.length, actualEnd);
        if (s == e - 1) return chunks[s];
        StringBuilder sb = new StringBuilder();
        for (int i = s; i < e; i++) {
            if (sb.length() > 0) sb.append(separator);
            sb.append(chunks[i]);
        }
        return sb.toString();
    }

    // Direct-scan methods delegated to StringChunkUtils to avoid duplication.

    private static int countItemsDirect(String str, char delimiter) {
        int count = 1;
        for (int i = 0; i < str.length(); i++) {
            if (str.charAt(i) == delimiter) count++;
        }
        return count;
    }

    private static String getItemDirect(String str, int index, char delimiter) {
        int current = 1;
        int start = 0;
        for (int i = 0; i < str.length(); i++) {
            if (str.charAt(i) == delimiter) {
                if (current == index) return str.substring(start, i);
                current++;
                start = i + 1;
            }
        }
        return current == index ? str.substring(start) : "";
    }

    private static int countWordsDirect(String str) {
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
    }

    private static int countLinesDirect(String str) {
        String delim = StringChunkUtils.pickLineDelimiter(str);
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

    // ========================================================================
    // Cached split methods — for long strings being iterated.
    // Simple char loops instead of regex for WASM performance.
    // ========================================================================

    // cachedSplitItems removed — ITEM chunks use direct scanning to avoid burst allocation

    /** Split by whitespace with single-entry cache. */
    private static String[] cachedSplitWords(String str) {
        if (str == _wordCacheStr && _wordCacheResult != null) {
            return _wordCacheResult;
        }
        String trimmed = str.trim();
        if (trimmed.isEmpty()) {
            _wordCacheStr = str;
            _wordCacheResult = new String[0];
            return _wordCacheResult;
        }
        // Simple whitespace split — no regex overhead
        ArrayList<String> words = new ArrayList<>();
        int wordStart = 0;
        boolean inWord = false;
        for (int i = 0; i <= trimmed.length(); i++) {
            boolean isSpace = i == trimmed.length() || Character.isWhitespace(trimmed.charAt(i));
            if (isSpace && inWord) {
                words.add(trimmed.substring(wordStart, i));
                inWord = false;
            } else if (!isSpace && !inWord) {
                wordStart = i;
                inWord = true;
            }
        }
        String[] result = words.toArray(new String[0]);
        _wordCacheStr = str;
        _wordCacheResult = result;
        return result;
    }

    /** Split by line delimiter with single-entry cache. */
    private static String[] cachedSplitLines(String str) {
        if (str == _lineCacheStr && _lineCacheResult != null) {
            return _lineCacheResult;
        }
        String lineDelimiter = StringChunkUtils.pickLineDelimiter(str);
        int delimLen = lineDelimiter.length();
        ArrayList<String> lines = new ArrayList<>();
        int start = 0;
        while (true) {
            int idx = str.indexOf(lineDelimiter, start);
            if (idx == -1) {
                lines.add(str.substring(start));
                break;
            }
            lines.add(str.substring(start, idx));
            start = idx + delimLen;
        }
        String[] result = lines.toArray(new String[0]);
        _lineCacheStr = str;
        _lineCacheResult = result;
        return result;
    }
}
