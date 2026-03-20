package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;
import com.libreshockwave.lingo.Opcode;

import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Script bytecode chunk (Lscr).
 * Contains the compiled Lingo bytecode for a script.
 */
public record ScriptChunk(
    DirectorFile file,
    ChunkId id,
    ScriptType scriptType,
    int behaviorFlags,
    List<Handler> handlers,
    List<LiteralEntry> literals,
    List<PropertyEntry> properties,
    List<GlobalEntry> globals,
    byte[] rawBytecode
) implements Chunk {

    @Override
    public ChunkType type() {
        return ChunkType.Lscr;
    }

    public enum ScriptType {
        SCORE(1),       // Frame/score scripts
        BEHAVIOR(2),    // Cast member behavior scripts
        MOVIE_SCRIPT(3), // Movie scripts (prepareMovie, startMovie, etc.)
        PARENT(7),      // Parent scripts
        UNKNOWN(-1);

        private final int code;

        ScriptType(int code) {
            this.code = code;
        }

        public static ScriptType fromCode(int code) {
            for (ScriptType type : values()) {
                if (type.code == code) return type;
            }
            return UNKNOWN;
        }
    }

    public record Handler(
        int nameId,
        int handlerVectorPos,
        int bytecodeLength,
        int bytecodeOffset,
        int argCount,
        int localCount,
        int globalsCount,
        int lineCount,
        List<Integer> argNameIds,
        List<Integer> localNameIds,
        List<Instruction> instructions,
        Map<Integer, Integer> bytecodeIndexMap  // Maps bytecode offset -> instruction index
    ) {
        /**
         * Get instruction index for a bytecode offset. Returns -1 if not found.
         */
        public int getInstructionIndex(int offset) {
            return bytecodeIndexMap.getOrDefault(offset, -1);
        }
        public record Instruction(
            int offset,
            Opcode opcode,
            int rawOpcode,
            int argument
        ) {
            @Override
            public String toString() {
                if (rawOpcode >= 0x40) {
                    return String.format("[%d] %s %d", offset, opcode.getMnemonic(), argument);
                } else {
                    return String.format("[%d] %s", offset, opcode.getMnemonic());
                }
            }
        }
    }

    public record LiteralEntry(
        int type,
        int offset,
        Object value,
        double numericValue
    ) {
        /** Convenience constructor for non-float literal types. */
        public LiteralEntry(int type, int offset, Object value) {
            this(type, offset, value, 0.0);
        }
    }

    public record PropertyEntry(
        int nameId
    ) {}

    public record GlobalEntry(
        int nameId
    ) {}

    /**
     * Get the reliable script type from the associated cast member.
     * This is the authoritative source - the scriptType() record field is unreliable.
     * @return The script type from the cast member, or the internal type as fallback
     */
    public ScriptType getScriptType() {
        if (file != null) {
            ScriptType type = file.getScriptType(this);
            if (type != null && type != ScriptType.UNKNOWN) {
                return type;
            }
        }
        // Fallback to internal (unreliable) type
        return scriptType != null ? scriptType : ScriptType.UNKNOWN;
    }

    /**
     * Get the name of this script from its associated cast member.
     * @return The script name, or empty string if not found
     */
    public String getScriptName() {
        if (file == null) return "";

        // Find the cast member that contains this script
        for (CastMemberChunk member : file.getCastMembers()) {
            if (member.isScript()) {
                // Get the script chunk for this member and compare
                ScriptChunk script = file.getScriptByContextId(member.scriptId());
                if (script != null && script.id() == this.id) {
                    String name = member.name();
                    return name != null ? name : "";
                }
            }
        }

        return "";
    }

    /**
     * Get a display name for this script.
     * Returns the script name in quotes if available, otherwise falls back to type + id.
     * @return A formatted display name like "MyScript" (MOVIE_SCRIPT) or MOVIE_SCRIPT #5
     */
    public String getDisplayName() {
        String name = getScriptName();
        String type = getScriptType().name();
        if (name != null && !name.isEmpty()) {
            return "\"" + name + "\" (" + type + ")";
        }
        return type + " #" + id;
    }

    /**
     * Get the name of a handler using the file's script names.
     * @param handler The handler to get the name for
     * @return The handler name, or a fallback like "handler#N"
     */
    public String getHandlerName(Handler handler) {
        if (file != null) {
            ScriptNamesChunk names = file.getScriptNamesForScript(this);
            if (names != null) {
                return names.getName(handler.nameId());
            }
        }
        return "handler#" + handler.nameId();
    }

    /**
     * Resolve a name ID using the file's script names.
     * @param nameId The name ID to resolve
     * @return The resolved name, or a fallback like "#N"
     */
    public String resolveName(int nameId) {
        if (file != null) {
            ScriptNamesChunk names = file.getScriptNamesForScript(this);
            if (names != null) {
                return names.getName(nameId);
            }
        }
        return "#" + nameId;
    }

    public Handler findHandler(String name, ScriptNamesChunk names) {
        if (names == null) return null;

        // Match dirplayer-rs: look up each handler's name and compare strings
        for (Handler h : handlers) {
            String handlerName = names.getName(h.nameId);
            if (handlerName.equalsIgnoreCase(name)) {
                return h;
            }
        }
        return null;
    }

    /**
     * Find a handler by name using the file's per-script Lnam.
     */
    public Handler findHandler(String name) {
        if (file == null) return null;
        return findHandler(name, file.getScriptNamesForScript(this));
    }

    /**
     * Get property names declared in this script.
     * Properties are instance variables for parent scripts (behaviors).
     * @param names The ScriptNamesChunk to resolve name IDs
     * @return List of property names
     */
    public List<String> getPropertyNames(ScriptNamesChunk names) {
        List<String> result = new ArrayList<>();
        if (names == null) return result;
        for (PropertyEntry prop : properties) {
            result.add(names.getName(prop.nameId()));
        }
        return result;
    }

    /**
     * Get global variable names declared in this script.
     * These are globals that this script accesses.
     * @param names The ScriptNamesChunk to resolve name IDs
     * @return List of global variable names
     */
    public List<String> getGlobalNames(ScriptNamesChunk names) {
        List<String> result = new ArrayList<>();
        if (names == null) return result;
        for (GlobalEntry global : globals) {
            result.add(names.getName(global.nameId()));
        }
        return result;
    }

    /**
     * Check if this script declares any properties.
     */
    public boolean hasProperties() {
        return !properties.isEmpty();
    }

    /**
     * Check if this script declares any globals.
     */
    public boolean hasGlobals() {
        return !globals.isEmpty();
    }

    public static ScriptChunk read(DirectorFile file, BinaryReader reader, ChunkId id, int version, boolean capitalX) {
        // Lingo scripts are ALWAYS big endian regardless of file byte order
        reader.setOrder(ByteOrder.BIG_ENDIAN);

        // Header - skip first 8 bytes
        reader.seek(8);

        int totalLength = reader.readI32();
        int totalLength2 = reader.readI32();
        int headerLength = reader.readU16();
        int scriptNumber = reader.readU16();

        // Seek to scriptBehavior at offset 38
        reader.seek(38);
        int behaviorFlags = reader.readI32();

        // Script type determination:
        // - Try behaviorFlags low nibble first
        // - Fall back to scriptNumber if that's 0
        int scriptTypeCode = behaviorFlags & 0x0F;
        if (scriptTypeCode == 0) {
            scriptTypeCode = scriptNumber;
        }

        // Debug output for script type investigation
        boolean debugScriptType = "true".equals(System.getProperty("libreshockwave.debug.scriptType"));
        if (debugScriptType) {
            System.out.println("[ScriptChunk] id=" + id + " scriptNumber=" + scriptNumber +
                " behaviorFlags=0x" + Integer.toHexString(behaviorFlags) +
                " (low nibble=" + (behaviorFlags & 0x0F) + ")" +
                " -> scriptTypeCode=" + scriptTypeCode +
                " -> " + ScriptType.fromCode(scriptTypeCode));
        }

        // Seek to handler data at offset 50
        reader.seek(50);
        int handlerVectorsCount = reader.readU16();
        int handlerVectorsOffset = reader.readI32();
        int handlerVectorsSize = reader.readI32();
        int propertyCount = reader.readU16();
        int propertiesOffset = reader.readI32();
        int globalCount = reader.readU16();
        int globalsOffset = reader.readI32();
        int handlerInfoCount = reader.readU16();
        int handlersOffset = reader.readI32();
        int literalCount = reader.readU16();
        int literalsOffset = reader.readI32();
        int literalDataLen = reader.readI32();
        int literalDataOffset = reader.readI32();

        // Use the script type derived from behaviorFlags or scriptNumber
        int scriptType = scriptTypeCode;

        // Read properties
        List<PropertyEntry> properties = new ArrayList<>();
        if (propertyCount > 0 && propertiesOffset > 0) {
            reader.setPosition(propertiesOffset);
            for (int i = 0; i < propertyCount; i++) {
                properties.add(new PropertyEntry(reader.readI16()));
            }
        }

        // Read globals
        List<GlobalEntry> globals = new ArrayList<>();
        if (globalCount > 0 && globalsOffset > 0) {
            reader.setPosition(globalsOffset);
            for (int i = 0; i < globalCount; i++) {
                globals.add(new GlobalEntry(reader.readI16()));
            }
        }

        // Read literals
        List<LiteralEntry> literals = new ArrayList<>();
        if (literalCount > 0 && literalsOffset > 0) {
            reader.setPosition(literalsOffset);
            List<int[]> literalInfo = new ArrayList<>();
            for (int i = 0; i < literalCount; i++) {
                // Pre-D5: literal type is u16; D5+: u32 (see ProjectorRays subchunk.cpp LiteralStore::readRecord)
                int type = (version < 0x4B1) ? reader.readU16() : reader.readI32();
                int offset = reader.readI32();
                literalInfo.add(new int[]{type, offset});
            }

            // Read literal data
            for (int[] info : literalInfo) {
                int type = info[0];
                int offset = info[1];
                Object value = null;

                reader.setPosition(literalDataOffset + offset);
                int dataLen = reader.readI32();

                double numericValue = 0.0;
                switch (type) {
                    case 1 -> { // String (dataLen includes null terminator)
                        String s = reader.readStringMacRoman(dataLen);
                        // Strip null terminator if present
                        if (s.endsWith("\0")) {
                            s = s.substring(0, s.length() - 1);
                        }
                        value = s;
                    }
                    case 4 -> { // Int
                        value = dataLen; // Actually the value
                    }
                    case 9 -> { // Float — store as primitive double to avoid boxed Double (TeaVM WASM bug)
                        numericValue = (double) Float.intBitsToFloat(reader.readI32());
                        value = String.valueOf(numericValue);
                    }
                    default -> {
                        value = reader.readBytes(dataLen);
                    }
                }

                literals.add(new LiteralEntry(type, offset, value, numericValue));
            }
        }

        // Read handlers
        List<Handler> handlers = new ArrayList<>();
        if (handlerInfoCount > 0 && handlersOffset > 0) {
            reader.setPosition(handlersOffset);

            for (int i = 0; i < handlerInfoCount; i++) {
                // Handler record structure from ProjectorRays handler.cpp:readRecord()
                int nameId = reader.readI16();           // int16
                int handlerVectorPos = reader.readU16(); // uint16
                int bytecodeLen = reader.readI32();      // uint32
                int bytecodeOffset = reader.readI32();   // uint32
                int argCount = reader.readU16();         // uint16
                int argOffset = reader.readI32();        // uint32
                int localCount = reader.readU16();       // uint16
                int localOffset = reader.readI32();      // uint32
                int handlerGlobalsCount = reader.readU16();   // uint16
                int handlerGlobalsOffset = reader.readI32();  // uint32
                int unknown1 = reader.readI32();         // uint32
                int unknown2 = reader.readU16();         // uint16
                int lineCount = reader.readU16();        // uint16
                int lineOffset = reader.readI32();       // uint32
                if (capitalX) {
                    int stackHeight = reader.readI32();  // uint32 (only in LctX)
                }

                int savedPos = reader.getPosition();

                // Read argument names (using unsigned 16-bit per ProjectorRays)
                List<Integer> argNameIds = new ArrayList<>();
                if (argCount > 0 && argOffset > 0) {
                    reader.setPosition(argOffset);
                    for (int j = 0; j < argCount; j++) {
                        argNameIds.add(reader.readU16());
                    }
                }

                // Read local variable names (using unsigned 16-bit per ProjectorRays)
                List<Integer> localNameIds = new ArrayList<>();
                if (localCount > 0 && localOffset > 0) {
                    reader.setPosition(localOffset);
                    for (int j = 0; j < localCount; j++) {
                        localNameIds.add(reader.readU16());
                    }
                }

                // Parse bytecode instructions (matching dirplayer-rs handler.rs)
                List<Handler.Instruction> instructions = new ArrayList<>();
                Map<Integer, Integer> bytecodeIndexMap = new HashMap<>();
                if (bytecodeLen > 0 && bytecodeOffset > 0) {
                    reader.setPosition(bytecodeOffset);
                    int bytecodeEnd = bytecodeOffset + bytecodeLen;

                    while (reader.getPosition() < bytecodeEnd) {
                        if (com.libreshockwave.DirectorFile.isParseTimedOut()) break;
                        int instrOffset = reader.getPosition() - bytecodeOffset;
                        int op = reader.readU8();

                        // Normalize opcode: multi-byte opcodes are 0x40 + (op % 0x40)
                        Opcode opcode = Opcode.fromCode(op >= 0x40 ? (0x40 + op % 0x40) : op);
                        int argument = 0;

                        // Argument size is determined by the op byte value, not opcode type
                        if (op >= 0xC0) {
                            // 4-byte argument
                            argument = reader.readI32();
                        } else if (op >= 0x80) {
                            // 2-byte argument
                            if (opcode == Opcode.PUSH_INT16 || opcode == Opcode.PUSH_INT8) {
                                // Treat pushint's arg as signed
                                argument = reader.readI16();
                            } else {
                                argument = reader.readU16();
                            }
                        } else if (op >= 0x40) {
                            // 1-byte argument
                            if (opcode == Opcode.PUSH_INT8) {
                                // Treat pushint's arg as signed
                                argument = reader.readI8();
                            } else {
                                argument = reader.readU8();
                            }
                        }
                        // op < 0x40: no argument (single-byte opcode)

                        // Build bytecode index map: offset -> instruction index
                        bytecodeIndexMap.put(instrOffset, instructions.size());
                        instructions.add(new Handler.Instruction(instrOffset, opcode, op, argument));
                    }
                }

                reader.setPosition(savedPos);

                handlers.add(new Handler(
                    nameId,
                    handlerVectorPos,
                    bytecodeLen,
                    bytecodeOffset,
                    argCount,
                    localCount,
                    handlerGlobalsCount,
                    lineCount,
                    argNameIds,
                    localNameIds,
                    instructions,
                    bytecodeIndexMap
                ));
            }
        }

        // Read raw bytecode for reference
        byte[] rawBytecode = new byte[0];

        return new ScriptChunk(
            file,
            id,
            ScriptType.fromCode(scriptType),
            behaviorFlags,
            handlers,
            literals,
            properties,
            globals,
            rawBytecode
        );
    }
}
