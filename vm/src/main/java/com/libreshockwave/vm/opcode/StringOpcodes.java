package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.lingo.StringChunkType;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.util.StringChunkUtils;

import java.util.List;
import java.util.Map;

/**
 * String operation opcodes.
 */
public final class StringOpcodes {

    private StringOpcodes() {}

    // Cache for single-character Datum.Str values (ASCII range).
    // GET_CHUNK(char X of str) is the hottest opcode during the dump handler —
    // ~540K calls, each previously allocating ChunkBounds + String + Datum.Str.
    // This cache + charAt fast path achieves ZERO allocations for ASCII chars.
    private static final Datum[] SINGLE_CHAR_DATUMS = new Datum[128];
    static {
        for (int i = 0; i < 128; i++) {
            SINGLE_CHAR_DATUMS[i] = Datum.of(String.valueOf((char) i));
        }
    }

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.JOIN_STR, StringOpcodes::joinStr);
        handlers.put(Opcode.JOIN_PAD_STR, StringOpcodes::joinPadStr);
        handlers.put(Opcode.CONTAINS_STR, StringOpcodes::containsStr);
        handlers.put(Opcode.CONTAINS_0_STR, StringOpcodes::contains0Str);
        handlers.put(Opcode.GET_CHUNK, StringOpcodes::getChunk);
        handlers.put(Opcode.PUT, StringOpcodes::put);
        handlers.put(Opcode.PUT_CHUNK, StringOpcodes::putChunk);
        handlers.put(Opcode.DELETE_CHUNK, StringOpcodes::deleteChunk);
    }

    private static boolean joinStr(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        // Skip concat + Datum.of allocation when either side is empty (common in loops)
        String aStr = a.toStr();
        String bStr = b.toStr();
        // Only reuse original Datum if it's already a string to avoid type confusion
        // (e.g., Int(0) & "" must produce Str("0"), not Int(0))
        if (aStr.isEmpty()) { ctx.push(b instanceof Datum.Str ? b : Datum.of(bStr)); return true; }
        if (bStr.isEmpty()) { ctx.push(a instanceof Datum.Str ? a : Datum.of(aStr)); return true; }
        String result = aStr + bStr;
        ctx.push(Datum.of(result));
        return true;
    }

    private static boolean joinPadStr(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        String aStr = a.toStr();
        String bStr = b.toStr();
        if (aStr.isEmpty()) { ctx.push(b instanceof Datum.Str ? b : Datum.of(bStr)); return true; }
        if (bStr.isEmpty()) { ctx.push(a instanceof Datum.Str ? a : Datum.of(aStr)); return true; }
        ctx.push(Datum.of(aStr + " " + bStr));
        return true;
    }

    private static boolean containsStr(ExecutionContext ctx) {
        Datum needle = ctx.pop();
        Datum haystack = ctx.pop();
        // Director's "contains" operator is case-insensitive (confirmed via ScummVM's
        // c_contains which normalizeString() lowercases before comparing)
        String h = haystack.toStr();
        String n = needle.toStr();
        if (n.isEmpty()) {
            ctx.push(Datum.FALSE);
            return true;
        }
        boolean contains = h.toLowerCase().contains(n.toLowerCase());
        ctx.push(contains ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    /**
     * CONTAINS_0_STR (0x16) - "starts with" check (case-insensitive).
     * Stack: [..., haystack, needle] -> [..., result]
     */
    private static boolean contains0Str(ExecutionContext ctx) {
        Datum needle = ctx.pop();
        Datum haystack = ctx.pop();

        boolean result;
        if (haystack.isVoid()) {
            result = false;
        } else {
            // Use regionMatches to avoid 2 toLowerCase() String allocations
            String h = haystack.toStr();
            String n = needle.toStr();
            result = !n.isEmpty() && h.regionMatches(true, 0, n, 0, n.length());
        }

        ctx.push(result ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    /**
     * GET_CHUNK (0x17) - Extract a string chunk.
     * Pops string, then 8 chunk parameters, extracts the chunk.
     * Stack: [..., firstChar, lastChar, firstWord, lastWord, firstItem, lastItem, firstLine, lastLine, string]
     *     -> [..., chunkValue]
     *
     * Optimized: chunk bounds are inlined (no ChunkBounds record allocation).
     * Single-char extraction uses charAt + cached Datum for ZERO allocations.
     */
    private static boolean getChunk(ExecutionContext ctx) {
        Datum stringDatum = ctx.pop();

        // Pop 8 chunk bounds directly (avoids ChunkBounds record allocation)
        int lastLine = ctx.pop().toInt();
        int firstLine = ctx.pop().toInt();
        int lastItem = ctx.pop().toInt();
        int firstItem = ctx.pop().toInt();
        int lastWord = ctx.pop().toInt();
        int firstWord = ctx.pop().toInt();
        int lastChar = ctx.pop().toInt();
        int firstChar = ctx.pop().toInt();

        // Fast path: single char extraction (char X of str) — ZERO allocations
        // This is the hottest path during the dump: ~540K calls in replaceChunks loops.
        if (firstChar != 0 && lastChar == 0
            && firstWord == 0 && lastWord == 0
            && firstItem == 0 && lastItem == 0
            && firstLine == 0 && lastLine == 0) {
            String str = stringDatum.toStr();
            // Director uses negative indices for "the last" (e.g., -30003)
            int fc = firstChar < 0 ? str.length() : firstChar;
            int idx = fc - 1;
            if (idx >= 0 && idx < str.length()) {
                char c = str.charAt(idx);
                ctx.push(c < 128 ? SINGLE_CHAR_DATUMS[c] : Datum.of(String.valueOf(c)));
            } else {
                ctx.push(Datum.EMPTY_STRING);
            }
            return true;
        }

        // Fast path: char range (char X to Y of str), no other chunk types
        if (firstChar != 0 && lastChar != 0
            && firstWord == 0 && lastWord == 0
            && firstItem == 0 && lastItem == 0
            && firstLine == 0 && lastLine == 0) {
            String str = stringDatum.toStr();
            // Director uses negative indices for "the last" (e.g., -30003)
            int fc = firstChar < 0 ? str.length() : firstChar;
            int lc = lastChar < 0 ? str.length() : lastChar;
            int start = fc - 1;
            int end = Math.min(lc, str.length());
            if (start >= 0 && start < str.length() && end > start) {
                ctx.push(Datum.of(str.substring(start, end)));
            } else {
                ctx.push(Datum.EMPTY_STRING);
            }
            return true;
        }

        // General path: apply chunks sequentially (largest to smallest granularity)
        String str = stringDatum.toStr();
        char itemDelimiter = getItemDelimiter();
        String result = str;

        if (firstLine != 0 || lastLine != 0) {
            result = resolveChunkRange(result, StringChunkType.LINE, firstLine, lastLine, itemDelimiter);
        }
        if (firstItem != 0 || lastItem != 0) {
            result = resolveChunkRange(result, StringChunkType.ITEM, firstItem, lastItem, itemDelimiter);
        }
        if (firstWord != 0 || lastWord != 0) {
            result = resolveChunkRange(result, StringChunkType.WORD, firstWord, lastWord, itemDelimiter);
        }
        if (firstChar != 0 || lastChar != 0) {
            result = resolveChunkRange(result, StringChunkType.CHAR, firstChar, lastChar, itemDelimiter);
        }

        ctx.push(Datum.of(result));
        return true;
    }

    // Context variable types (matching dirplayer-rs ContextVars)
    private static final int VAR_TYPE_GLOBAL1 = 0x1;
    private static final int VAR_TYPE_GLOBAL2 = 0x2;
    private static final int VAR_TYPE_PROPERTY = 0x3;
    private static final int VAR_TYPE_ARG = 0x4;
    private static final int VAR_TYPE_LOCAL = 0x5;
    private static final int VAR_TYPE_FIELD = 0x6;

    /**
     * PUT (0x59) - Put a value into/before/after a variable.
     * Argument encoding: upper nibble = put type, lower nibble = var type.
     * Stack depends on var type.
     */
    private static boolean put(ExecutionContext ctx) {
        int arg = ctx.getArgument();
        int putType = (arg >> 4) & 0xF;  // 0=INTO, 1=BEFORE, 2=AFTER
        int varType = arg & 0xF;

        // Read context var args (pop ID from stack, and castId for fields)
        Datum castIdDatum = null;
        if (varType == VAR_TYPE_FIELD) {
            castIdDatum = ctx.pop();
        }
        Datum idDatum = ctx.pop();

        // Pop the value to put
        Datum value = ctx.pop();

        switch (putType) {
            case 0: // INTO
                setContextVar(ctx, varType, idDatum, castIdDatum, value);
                break;
            case 1: { // BEFORE
                Datum current = getContextVar(ctx, varType, idDatum, castIdDatum);
                String valStr = value.toStr();
                String curStr = current.toStr();
                // Avoid concat allocation when one side is empty (common in loops)
                String newStr = curStr.isEmpty() ? valStr : valStr.isEmpty() ? curStr : valStr + curStr;
                setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newStr));
                break;
            }
            case 2: { // AFTER
                Datum current = getContextVar(ctx, varType, idDatum, castIdDatum);
                String curStr = current.toStr();
                String valStr = value.toStr();
                // Avoid concat allocation when one side is empty (common in loops)
                String newStr = curStr.isEmpty() ? valStr : valStr.isEmpty() ? curStr : curStr + valStr;
                setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newStr));
                break;
            }
        }

        return true;
    }

    /**
     * PUT_CHUNK (0x5A) - Put a value into/before/after a string chunk of a variable.
     * Argument encoding: upper nibble = put type, lower nibble = var type.
     * Optimized: inlined chunk bounds + CHAR fast path (no record allocations).
     */
    private static boolean putChunk(ExecutionContext ctx) {
        int arg = ctx.getArgument();
        int putType = (arg >> 4) & 0xF;
        int varType = arg & 0xF;

        // Read context var args
        Datum castIdDatum = null;
        if (varType == VAR_TYPE_FIELD) {
            castIdDatum = ctx.pop();
        }
        Datum idDatum = ctx.pop();

        // Pop the value
        Datum value = ctx.pop();

        // Pop 8 chunk bounds directly (avoids ChunkBounds + ChunkExpr record allocations)
        int lastLine = ctx.pop().toInt();
        int firstLine = ctx.pop().toInt();
        int lastItem = ctx.pop().toInt();
        int firstItem = ctx.pop().toInt();
        int lastWord = ctx.pop().toInt();
        int firstWord = ctx.pop().toInt();
        int lastChar = ctx.pop().toInt();
        int firstChar = ctx.pop().toInt();

        // Determine chunk type
        StringChunkType type;
        int first, last;
        if (firstLine != 0 || lastLine != 0) {
            type = StringChunkType.LINE; first = firstLine; last = lastLine;
        } else if (firstItem != 0 || lastItem != 0) {
            type = StringChunkType.ITEM; first = firstItem; last = lastItem;
        } else if (firstWord != 0 || lastWord != 0) {
            type = StringChunkType.WORD; first = firstWord; last = lastWord;
        } else if (firstChar != 0 || lastChar != 0) {
            type = StringChunkType.CHAR; first = firstChar; last = lastChar;
        } else {
            type = StringChunkType.CHAR; first = 1; last = 1; // default
        }

        // Get current string value
        Datum currentDatum = getContextVar(ctx, varType, idDatum, castIdDatum);
        String currentString = currentDatum.toStr();
        String valueString = value.toStr();
        char itemDelimiter = getItemDelimiter();

        // Fast path: CHAR type — directly compute byte range, zero record allocations.
        // Hot path for "put X after char Y of str" in replaceChunks.
        if (type == StringChunkType.CHAR) {
            String newString;
            switch (putType) {
                case 0: { // INTO
                    int[] range = getCharChunkReplaceRange(currentString, first, last);
                    if (range == null) {
                        return true;
                    }
                    newString = currentString.substring(0, range[0]) + valueString + currentString.substring(range[1]);
                    break;
                }
                case 1: { // BEFORE
                    int insertAt = getChunkInsertionIndex(currentString, type, first, true, itemDelimiter);
                    newString = currentString.substring(0, insertAt) + valueString + currentString.substring(insertAt);
                    break;
                }
                case 2: { // AFTER
                    int insertAt = getChunkInsertionIndex(currentString, type, last == 0 ? first : last, false, itemDelimiter);
                    newString = currentString.substring(0, insertAt) + valueString + currentString.substring(insertAt);
                    break;
                }
                default:
                    newString = currentString;
            }
            setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newString));
            return true;
        }

        // General path — resolve negative indices ("the last") to actual count
        if (first < 0 || last < 0) {
            int count = StringChunkUtils.countChunks(currentString, type, itemDelimiter);
            if (first < 0) first = count;
            if (last < 0) last = count;
        }
        ChunkExpr chunkExpr = new ChunkExpr(type, first, last);
        String newString;
        switch (putType) {
            case 0:
                newString = stringByPuttingIntoChunk(currentString, chunkExpr, valueString, itemDelimiter);
                break;
            case 1:
                newString = stringByPuttingBeforeChunk(currentString, chunkExpr, valueString, itemDelimiter);
                break;
            case 2:
                newString = stringByPuttingAfterChunk(currentString, chunkExpr, valueString, itemDelimiter);
                break;
            default:
                newString = currentString;
        }

        setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newString));
        return true;
    }

    /**
     * DELETE_CHUNK (0x5B) - Delete a string chunk from a variable.
     * Argument is just the var type (no put type).
     * Optimized: inlined chunk bounds + CHAR fast path (no record allocations).
     */
    private static boolean deleteChunk(ExecutionContext ctx) {
        int varType = ctx.getArgument();

        // Read context var args
        Datum castIdDatum = null;
        if (varType == VAR_TYPE_FIELD) {
            castIdDatum = ctx.pop();
        }
        Datum idDatum = ctx.pop();

        // Pop 8 chunk bounds directly (avoids ChunkBounds + ChunkExpr record allocations)
        int lastLine = ctx.pop().toInt();
        int firstLine = ctx.pop().toInt();
        int lastItem = ctx.pop().toInt();
        int firstItem = ctx.pop().toInt();
        int lastWord = ctx.pop().toInt();
        int firstWord = ctx.pop().toInt();
        int lastChar = ctx.pop().toInt();
        int firstChar = ctx.pop().toInt();

        // Determine chunk type
        StringChunkType type;
        int first, last;
        if (firstLine != 0 || lastLine != 0) {
            type = StringChunkType.LINE; first = firstLine; last = lastLine;
        } else if (firstItem != 0 || lastItem != 0) {
            type = StringChunkType.ITEM; first = firstItem; last = lastItem;
        } else if (firstWord != 0 || lastWord != 0) {
            type = StringChunkType.WORD; first = firstWord; last = lastWord;
        } else if (firstChar != 0 || lastChar != 0) {
            type = StringChunkType.CHAR; first = firstChar; last = lastChar;
        } else {
            type = StringChunkType.CHAR; first = 1; last = 1; // default
        }

        // Get current string
        Datum currentDatum = getContextVar(ctx, varType, idDatum, castIdDatum);
        String currentString = currentDatum.toStr();

        // Fast path: CHAR delete — directly compute range and delete, zero record allocations.
        // Hot path for "delete char X to Y of str" in replaceChunks.
        if (type == StringChunkType.CHAR) {
            // Director uses negative chunk indices to mean "from the end":
            // -30003 = the last char, -30002 = the last word, etc.
            // Convert negative first/last to actual string positions.
            if (first < 0) first = currentString.length();
            if (last < 0) last = currentString.length();
            if (first < 1) first = 1;
            int effectiveLast = last == 0 ? first : last;
            if (first > currentString.length()) {
                return true; // Nothing to delete
            }
            int deleteStart = first - 1;
            int deleteEnd = Math.min(effectiveLast, currentString.length());
            String newStr;
            if (deleteStart == 0) newStr = currentString.substring(deleteEnd);
            else if (deleteEnd >= currentString.length()) newStr = currentString.substring(0, deleteStart);
            else newStr = currentString.substring(0, deleteStart) + currentString.substring(deleteEnd);
            setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newStr));
            return true;
        }

        // General path — resolve negative indices ("the last") to actual count
        char itemDelimiter = getItemDelimiter();
        if (first < 0 || last < 0) {
            int count = StringChunkUtils.countChunks(currentString, type, itemDelimiter);
            if (first < 0) first = count;
            if (last < 0) last = count;
        }
        ChunkExpr chunkExpr = new ChunkExpr(type, first, last);
        String newString = stringByDeletingChunk(currentString, chunkExpr, itemDelimiter);
        setContextVar(ctx, varType, idDatum, castIdDatum, Datum.of(newString));

        return true;
    }

    // ==================== Helper methods ====================

    /**
     * Read a single chunk reference from the stack.
     * Pops 8 values: lastLine, firstLine, lastItem, firstItem, lastWord, firstWord, lastChar, firstChar.
     * Returns the first non-zero chunk specification.
     */
    private static ChunkExpr readSingleChunkRef(ExecutionContext ctx) {
        ChunkBounds cb = popChunkBounds(ctx);

        if (cb.firstLine() != 0 || cb.lastLine() != 0) {
            return new ChunkExpr(StringChunkType.LINE, cb.firstLine(), cb.lastLine());
        } else if (cb.firstItem() != 0 || cb.lastItem() != 0) {
            return new ChunkExpr(StringChunkType.ITEM, cb.firstItem(), cb.lastItem());
        } else if (cb.firstWord() != 0 || cb.lastWord() != 0) {
            return new ChunkExpr(StringChunkType.WORD, cb.firstWord(), cb.lastWord());
        } else if (cb.firstChar() != 0 || cb.lastChar() != 0) {
            return new ChunkExpr(StringChunkType.CHAR, cb.firstChar(), cb.lastChar());
        }

        // Default to char 1 to 1
        return new ChunkExpr(StringChunkType.CHAR, 1, 1);
    }

    private record ChunkExpr(StringChunkType type, int first, int last) {}

    /** Raw 8-value chunk boundaries popped from the VM stack. */
    private record ChunkBounds(int firstChar, int lastChar, int firstWord, int lastWord,
                               int firstItem, int lastItem, int firstLine, int lastLine) {}

    /** Pop 8 chunk boundary values from the stack (in reverse push order). */
    private static ChunkBounds popChunkBounds(ExecutionContext ctx) {
        int lastLine = ctx.pop().toInt();
        int firstLine = ctx.pop().toInt();
        int lastItem = ctx.pop().toInt();
        int firstItem = ctx.pop().toInt();
        int lastWord = ctx.pop().toInt();
        int firstWord = ctx.pop().toInt();
        int lastChar = ctx.pop().toInt();
        int firstChar = ctx.pop().toInt();
        return new ChunkBounds(firstChar, lastChar, firstWord, lastWord,
                firstItem, lastItem, firstLine, lastLine);
    }

    /**
     * Resolve a chunk range from a string.
     */
    private static String resolveChunkRange(String str, StringChunkType chunkType,
                                            int first, int last, char itemDelimiter) {
        if (first == 0 && last == 0) return str;
        // Director uses negative chunk indices to mean "from the end" (e.g., -30003 = the last char).
        // Resolve to actual count before extraction.
        if (first < 0 || last < 0) {
            int count = StringChunkUtils.countChunks(str, chunkType, itemDelimiter);
            if (first < 0) first = count;
            if (last < 0) last = count;
        }
        int effectiveLast = last == 0 ? first : last;
        if (first == effectiveLast) {
            return StringChunkUtils.getChunk(str, chunkType, first, itemDelimiter);
        }
        return StringChunkUtils.getChunkRange(str, chunkType, first, effectiveLast, itemDelimiter);
    }

    /**
     * Get the current item delimiter.
     */
    private static char getItemDelimiter() {
        return MoviePropertyProvider.ItemDelimiterCache._char;
    }

    /**
     * Get a context variable value.
     */
    private static Datum getContextVar(ExecutionContext ctx, int varType,
                                       Datum idDatum, Datum castIdDatum) {
        int variableMultiplier = ctx.getVariableMultiplier();

        switch (varType) {
            case VAR_TYPE_LOCAL: {
                int localIndex = idDatum.toInt() / variableMultiplier;
                return ctx.getLocal(localIndex);
            }
            case VAR_TYPE_ARG: {
                int argIndex = idDatum.toInt() / variableMultiplier;
                return ctx.getParam(argIndex);
            }
            case VAR_TYPE_FIELD: {
                CastLibProvider provider = CastLibProvider.getProvider();
                if (provider != null) {
                    int castId = castIdDatum != null ? castIdDatum.toInt() : 0;
                    Object identifier = idDatum instanceof Datum.Str s ? s.value()
                            : idDatum instanceof Datum.Int i ? i.value()
                            : idDatum.toStr();
                    return provider.getFieldDatum(identifier, castId);
                }
                return Datum.EMPTY_STRING;
            }
            case VAR_TYPE_GLOBAL1:
            case VAR_TYPE_GLOBAL2: {
                String name = ctx.resolveName(idDatum.toInt());
                return ctx.getGlobal(name);
            }
            case VAR_TYPE_PROPERTY: {
                // Property on current receiver
                Datum receiver = ctx.getReceiver();
                if (receiver instanceof Datum.ScriptInstance si) {
                    String propName = ctx.resolveName(idDatum.toInt());
                    Datum value = si.properties().get(propName);
                    return value != null ? value : Datum.VOID;
                }
                return Datum.VOID;
            }
            default: {
                System.err.println("[WARN] getContextVar: unsupported var type " + varType);
                return Datum.VOID;
            }
        }
    }

    /**
     * Set a context variable value.
     */
    private static void setContextVar(ExecutionContext ctx, int varType,
                                      Datum idDatum, Datum castIdDatum, Datum value) {
        int variableMultiplier = ctx.getVariableMultiplier();

        switch (varType) {
            case VAR_TYPE_LOCAL: {
                int localIndex = idDatum.toInt() / variableMultiplier;
                ctx.setLocal(localIndex, value);
                break;
            }
            case VAR_TYPE_ARG: {
                int argIndex = idDatum.toInt() / variableMultiplier;
                ctx.setParam(argIndex, value);
                break;
            }
            case VAR_TYPE_FIELD: {
                CastLibProvider provider = CastLibProvider.getProvider();
                if (provider != null) {
                    int castId = castIdDatum != null ? castIdDatum.toInt() : 0;
                    Object identifier = idDatum instanceof Datum.Str s ? s.value()
                            : idDatum instanceof Datum.Int i ? i.value()
                            : idDatum.toStr();
                    provider.setFieldValue(identifier, castId, value.toStr());
                }
                break;
            }
            case VAR_TYPE_GLOBAL1:
            case VAR_TYPE_GLOBAL2: {
                String name = ctx.resolveName(idDatum.toInt());
                ctx.setGlobal(name, value);
                break;
            }
            case VAR_TYPE_PROPERTY: {
                Datum receiver = ctx.getReceiver();
                if (receiver instanceof Datum.ScriptInstance si) {
                    String propName = ctx.resolveName(idDatum.toInt());
                    si.properties().put(propName, value);
                }
                break;
            }
            default:
                System.err.println("[WARN] setContextVar: unsupported var type " + varType);
        }
    }

    // ==================== String chunk manipulation ====================

    /**
     * Replace a chunk in a string with a new value (put X into chunk Y of Z).
     */
    private static String stringByPuttingIntoChunk(String str, ChunkExpr chunk,
                                                    String value, char itemDelimiter) {
        int[] range = getChunkByteRange(str, chunk, itemDelimiter);
        if (range == null) return str;
        return str.substring(0, range[0]) + value + str.substring(range[1]);
    }

    /**
     * Insert a value before a chunk (put X before chunk Y of Z).
     */
    private static String stringByPuttingBeforeChunk(String str, ChunkExpr chunk,
                                                      String value, char itemDelimiter) {
        int insertAt = getChunkInsertionIndex(str, chunk.type(), chunk.first(), true, itemDelimiter);
        return str.substring(0, insertAt) + value + str.substring(insertAt);
    }

    /**
     * Insert a value after a chunk (put X after chunk Y of Z).
     */
    private static String stringByPuttingAfterChunk(String str, ChunkExpr chunk,
                                                     String value, char itemDelimiter) {
        int targetIndex = chunk.last() == 0 ? chunk.first() : chunk.last();
        int insertAt = getChunkInsertionIndex(str, chunk.type(), targetIndex, false, itemDelimiter);
        return str.substring(0, insertAt) + value + str.substring(insertAt);
    }

    /**
     * Delete a chunk from a string.
     */
    private static String stringByDeletingChunk(String str, ChunkExpr chunk, char itemDelimiter) {
        int[] range = getChunkByteRange(str, chunk, itemDelimiter);
        if (range == null) return str;

        int deleteStart = range[0];
        int deleteEnd = range[1];

        // CHAR type has no delimiter — skip delimiter handling entirely.
        // This is the hot path for replaceChunks (delete char[1..N]).
        if (chunk.type() != StringChunkType.CHAR) {
            // For LINE type, detect actual delimiter from the source string
            String delim = (chunk.type() == StringChunkType.LINE) ? getLineDelimiter(str) : getChunkDelimiter(chunk.type(), itemDelimiter);
            // Try to consume trailing delimiter
            if (deleteEnd < str.length()) {
                if (delim.length() > 0 && str.startsWith(delim, deleteEnd)) {
                    deleteEnd += delim.length();
                }
            } else if (deleteStart > 0) {
                if (delim.length() > 0 && str.substring(0, deleteStart).endsWith(delim)) {
                    deleteStart -= delim.length();
                }
            }
        }

        // Optimize edge deletions: avoid creating empty substring + concat
        if (deleteStart == 0) return str.substring(deleteEnd);
        if (deleteEnd >= str.length()) return str.substring(0, deleteStart);
        return str.substring(0, deleteStart) + str.substring(deleteEnd);
    }

    /**
     * Get the byte range [start, end) of a chunk in a string.
     * Returns null if the chunk is out of range.
     *
     * Optimized to avoid splitIntoChunks for CHAR and ITEM types:
     * - CHAR: O(1) direct index computation, zero allocations.
     *   Previously created str.length() String objects per call (~1.8M during dump).
     * - ITEM: O(n) scan for delimiter positions, zero List allocation.
     * - WORD/LINE: uses cached split (existing behavior).
     */
    private static int[] getChunkByteRange(String str, ChunkExpr chunk, char itemDelimiter) {
        int first = chunk.first();
        int last = chunk.last() == 0 ? first : chunk.last();
        if (first < 1) first = 1;

        StringChunkType type = chunk.type();

        // Fast path: CHAR — each chunk is one character, delimiter is empty.
        // Byte range = [first-1, min(last, length)]. Zero allocations.
        if (type == StringChunkType.CHAR) {
            if (first > str.length()) return null;
            int end = Math.min(last, str.length());
            return new int[] { first - 1, end };
        }

        // Fast path: ITEM — scan for delimiter positions without creating List.
        if (type == StringChunkType.ITEM) {
            return getItemByteRange(str, first, last, itemDelimiter);
        }

        // WORD/LINE: use existing split+cache approach
        List<String> chunks = StringChunkUtils.splitIntoChunks(str, type, itemDelimiter);
        if (chunks.isEmpty()) return null;
        if (first > chunks.size()) return null;
        if (last > chunks.size()) last = chunks.size();

        // For LINE type, detect the actual delimiter from the source string
        // (may be \r, \n, or \r\n). Using a fixed \r\n causes off-by-one when
        // the text uses single-char \r delimiters (e.g., "Staff HQ\r..." loses first char).
        String delimiter = (type == StringChunkType.LINE) ? getLineDelimiter(str) : getChunkDelimiter(type, itemDelimiter);
        int start = 0;
        for (int i = 0; i < first - 1; i++) {
            start += chunks.get(i).length();
            if (i < chunks.size() - 1) {
                start += delimiter.length();
            }
        }

        int end = start;
        for (int i = first - 1; i < last; i++) {
            end += chunks.get(i).length();
            if (i < last - 1 && i < chunks.size() - 1) {
                end += delimiter.length();
            }
        }

        return new int[] { start, end };
    }

    /**
     * Scan for item byte range without creating a List of substrings.
     */
    private static int[] getItemByteRange(String str, int first, int last, char delimiter) {
        int chunkNum = 1;
        int chunkStart = 0;
        int rangeStart = -1;

        for (int i = 0; i <= str.length(); i++) {
            if (i == str.length() || str.charAt(i) == delimiter) {
                if (chunkNum == first) rangeStart = chunkStart;
                if (chunkNum == last) {
                    return rangeStart >= 0 ? new int[] { rangeStart, i } : null;
                }
                if (chunkNum > last) break;
                chunkNum++;
                chunkStart = i + 1;
            }
        }
        return (rangeStart >= 0) ? new int[] { rangeStart, str.length() } : null;
    }

    /**
     * Resolve a replacement range for CHAR chunks.
     * Returns null when the target chunk does not exist or the range is inverted.
     */
    private static int[] getCharChunkReplaceRange(String str, int first, int last) {
        int length = str.length();
        int normalizedFirst = normalizeCharChunkIndex(first, length, true);
        int normalizedLast = normalizeCharChunkIndex(last == 0 ? first : last, length, true);
        if (normalizedFirst < 1 || normalizedFirst > length) {
            return null;
        }
        if (normalizedLast < normalizedFirst) {
            return null;
        }
        int rangeStart = normalizedFirst - 1;
        int rangeEnd = Math.min(normalizedLast, length);
        if (rangeEnd < rangeStart) {
            return null;
        }
        return new int[] { rangeStart, rangeEnd };
    }

    /**
     * Resolve an insertion point for BEFORE/AFTER chunk operations.
     * Missing chunks clamp to the nearest valid boundary instead of becoming a no-op.
     */
    private static int getChunkInsertionIndex(String str, StringChunkType type, int targetIndex,
                                              boolean before, char itemDelimiter) {
        if (type == StringChunkType.CHAR) {
            int length = str.length();
            int normalized = normalizeCharChunkIndex(targetIndex, length, before);
            if (before) {
                if (normalized <= 1) {
                    return 0;
                }
                if (normalized > length) {
                    return length;
                }
                return normalized - 1;
            }
            if (normalized <= 0) {
                return 0;
            }
            if (normalized >= length) {
                return length;
            }
            return normalized;
        }

        int[] range = getChunkByteRange(str, new ChunkExpr(type, targetIndex, targetIndex), itemDelimiter);
        if (range != null) {
            return before ? range[0] : range[1];
        }

        if (before) {
            return targetIndex <= 1 ? 0 : str.length();
        }
        return targetIndex <= 0 ? 0 : str.length();
    }

    private static int normalizeCharChunkIndex(int index, int length, boolean before) {
        if (index < 0) {
            return length;
        }
        if (before) {
            if (index < 1) {
                return 1;
            }
            return index;
        }
        if (index == 0) {
            return 0;
        }
        return index;
    }

    /**
     * Get the delimiter string for a chunk type.
     * For LINE type, requires the source string to detect the actual delimiter
     * (which may be \r, \n, or \r\n depending on the text content).
     */
    private static String getChunkDelimiter(StringChunkType type, char itemDelimiter) {
        return switch (type) {
            case CHAR -> "";
            case WORD -> " ";
            case LINE -> "\r\n"; // default; callers with a source string should use getLineDelimiter()
            case ITEM -> String.valueOf(itemDelimiter);
        };
    }

    /**
     * Get the actual line delimiter for a given string.
     * Uses the same detection as splitIntoChunks to ensure consistent behavior.
     */
    private static String getLineDelimiter(String str) {
        return StringChunkUtils.pickLineDelimiter(str);
    }
}
