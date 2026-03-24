package com.libreshockwave.lingo.decompiler;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.format.ScriptFormatUtils;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.lingo.decompiler.LingoNode.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.libreshockwave.lingo.decompiler.LingoProperties.*;

/**
 * Lingo bytecode decompiler. Reconstructs Lingo source code from compiled bytecode.
 * Ported from ProjectorRays (handler.cpp translate/translateBytecode).
 */
public class LingoDecompiler {

    // Bytecode tags for loop identification
    private static final int TAG_NONE = 0, TAG_SKIP = 1, TAG_REPEAT_WHILE = 2,
        TAG_REPEAT_WITH_IN = 3, TAG_REPEAT_WITH_TO = 4, TAG_REPEAT_WITH_DOWN_TO = 5,
        TAG_NEXT_REPEAT_TARGET = 6;

    private ScriptChunk script;
    private ScriptNamesChunk names;
    private int version;
    private boolean capitalX;
    private boolean dotSyntax;

    // Per-handler state
    private ScriptChunk.Handler currentHandler;
    private List<ScriptChunk.Handler.Instruction> bytecodes;
    private Map<Integer, Integer> posMap; // offset -> instruction index
    private int[] tags;
    private int[] ownerLoops; // maps instruction index -> loop start index
    private List<LingoNode> stack;

    // AST state
    private HandlerNode astRoot;
    private BlockNode currentBlock;

    /**
     * Decompile a full script to Lingo source text.
     */
    public String decompile(ScriptChunk script, ScriptNamesChunk names) {
        this.script = script;
        this.names = names;
        initFileInfo();

        StringBuilder sb = new StringBuilder();
        String scriptType = ScriptFormatUtils.getScriptTypeName(script.getScriptType());
        sb.append("-- ").append(scriptType);
        String scriptName = script.getScriptName();
        if (scriptName != null && !scriptName.isEmpty()) {
            sb.append(": ").append(scriptName);
        }
        sb.append("\n\n");

        // Properties
        for (ScriptChunk.PropertyEntry prop : script.properties()) {
            sb.append("property ").append(resolveName(prop.nameId())).append("\n");
        }
        if (!script.properties().isEmpty()) sb.append("\n");

        // Globals
        for (ScriptChunk.GlobalEntry global : script.globals()) {
            sb.append("global ").append(resolveName(global.nameId())).append("\n");
        }
        if (!script.globals().isEmpty()) sb.append("\n");

        // Handlers
        for (ScriptChunk.Handler handler : script.handlers()) {
            try {
                sb.append(decompileHandler(handler));
            } catch (Exception e) {
                sb.append("-- ERROR decompiling handler: ").append(e.getMessage()).append("\n");
                sb.append(formatHandlerBytecodeOnly(handler));
            }
            sb.append("\n");
        }

        return sb.toString();
    }

    /**
     * Decompile a single handler to Lingo source text.
     */
    public String decompileHandler(ScriptChunk.Handler handler, ScriptChunk script, ScriptNamesChunk names) {
        this.script = script;
        this.names = names;
        initFileInfo();
        try {
            return decompileHandler(handler);
        } catch (Exception e) {
            return "-- ERROR decompiling handler: " + e.getMessage() + "\n"
                + formatHandlerBytecodeOnly(handler);
        }
    }

    private void initFileInfo() {
        DirectorFile file = script.file();
        if (file != null) {
            version = file.getVersion();
            capitalX = file.isCapitalX();
        } else {
            version = 0x4C1; // Default D5
            capitalX = false;
        }
        dotSyntax = version >= 700;
    }

    private String decompileHandler(ScriptChunk.Handler handler) {
        currentHandler = handler;
        bytecodes = handler.instructions();
        posMap = handler.bytecodeIndexMap();

        // Resolve handler metadata
        String handlerName = resolveName(handler.nameId());
        List<String> argNames = new ArrayList<>();
        for (int argId : handler.argNameIds()) {
            argNames.add(resolveName(argId));
        }
        List<String> globalNamesList = new ArrayList<>();
        // Note: handler-level globals come from the handler's global name IDs
        // but our ScriptChunk.Handler doesn't store them separately.
        // Globals are declared at script level instead.

        // Init tags
        tags = new int[bytecodes.size()];
        ownerLoops = new int[bytecodes.size()];
        for (int i = 0; i < ownerLoops.length; i++) ownerLoops[i] = -1;

        tagLoops();

        // Init stack and AST
        stack = new ArrayList<>();
        astRoot = new HandlerNode(handlerName, argNames, globalNamesList);
        currentBlock = astRoot.block;

        // Translate
        int i = 0;
        while (i < bytecodes.size()) {
            var bc = bytecodes.get(i);
            int pos = bc.offset();

            // Exit blocks that have ended
            while (pos == currentBlock.endPos) {
                BlockNode exitedBlock = currentBlock;
                LingoNode ancestorStmt = currentBlock.ancestorStatement();
                exitBlock();
                if (ancestorStmt instanceof IfStmtNode ifStmt) {
                    if (ifStmt.hasElse && exitedBlock == ifStmt.block1) {
                        enterBlock(ifStmt.block2);
                    }
                } else if (ancestorStmt instanceof CasesStmtNode) {
                    CaseNode caseNode = currentBlock.currentCase;
                    if (caseNode != null) {
                        if (caseNode.expect == CaseNode.EXPECT_OTHERWISE) {
                            if (exitedBlock == caseNode.block) {
                                caseNode.otherwise = new BlockNode();
                                caseNode.otherwise.parent = caseNode;
                                caseNode.otherwise.endPos = ((CasesStmtNode) ancestorStmt).endPos;
                                enterBlock(caseNode.otherwise);
                            } else {
                                currentBlock.currentCase = null;
                            }
                        } else if (caseNode.expect == CaseNode.EXPECT_POP) {
                            currentBlock.currentCase = null;
                        }
                    }
                }
            }

            int consumed = translateBytecode(bc, i);
            i += consumed;
        }

        return astRoot.toLingo(dotSyntax);
    }

    // ==================== Loop Tagging ====================

    private void tagLoops() {
        for (int startIdx = 0; startIdx < bytecodes.size(); startIdx++) {
            var jmpifz = bytecodes.get(startIdx);
            if (jmpifz.opcode() != Opcode.JMP_IF_Z) continue;

            int jmpPos = jmpifz.offset() + jmpifz.argument();
            Integer endIdxObj = posMap.get(jmpPos);
            if (endIdxObj == null || endIdxObj < 1) continue;
            int endIdx = endIdxObj;

            var endRepeat = bytecodes.get(endIdx - 1);
            if (endRepeat.opcode() != Opcode.END_REPEAT) continue;
            if ((endRepeat.offset() - endRepeat.argument()) > jmpifz.offset()) continue;

            int loopType = identifyLoop(startIdx, endIdx);
            tags[startIdx] = loopType;

            if (loopType == TAG_REPEAT_WITH_IN) {
                if (startIdx >= 7 && startIdx + 5 < bytecodes.size() && endIdx >= 3) {
                    for (int j = startIdx - 7; j <= startIdx - 1; j++) tags[j] = TAG_SKIP;
                    for (int j = startIdx + 1; j <= startIdx + 5; j++) tags[j] = TAG_SKIP;
                    tags[endIdx - 3] = TAG_NEXT_REPEAT_TARGET;
                    ownerLoops[endIdx - 3] = startIdx;
                    tags[endIdx - 2] = TAG_SKIP;
                    tags[endIdx - 1] = TAG_SKIP;
                    ownerLoops[endIdx - 1] = startIdx;
                    if (endIdx < bytecodes.size()) tags[endIdx] = TAG_SKIP;
                }
            } else if (loopType == TAG_REPEAT_WITH_TO || loopType == TAG_REPEAT_WITH_DOWN_TO) {
                int condStart = posMap.getOrDefault(endRepeat.offset() - endRepeat.argument(), -1);
                if (condStart >= 1 && condStart < bytecodes.size() && endIdx >= 5) {
                    tags[condStart - 1] = TAG_SKIP;
                    tags[condStart] = TAG_SKIP;
                    if (startIdx >= 1) tags[startIdx - 1] = TAG_SKIP;
                    tags[endIdx - 5] = TAG_NEXT_REPEAT_TARGET;
                    ownerLoops[endIdx - 5] = startIdx;
                    tags[endIdx - 4] = TAG_SKIP;
                    tags[endIdx - 3] = TAG_SKIP;
                    tags[endIdx - 2] = TAG_SKIP;
                    tags[endIdx - 1] = TAG_SKIP;
                    ownerLoops[endIdx - 1] = startIdx;
                }
            } else if (loopType == TAG_REPEAT_WHILE) {
                tags[endIdx - 1] = TAG_NEXT_REPEAT_TARGET;
                ownerLoops[endIdx - 1] = startIdx;
            }
        }
    }

    private int identifyLoop(int startIdx, int endIdx) {
        if (isRepeatWithIn(startIdx, endIdx)) return TAG_REPEAT_WITH_IN;
        if (startIdx < 1) return TAG_REPEAT_WHILE;

        boolean up;
        switch (bytecodes.get(startIdx - 1).opcode()) {
            case LT_EQ: up = true; break;
            case GT_EQ: up = false; break;
            default: return TAG_REPEAT_WHILE;
        }

        var endRepeat = bytecodes.get(endIdx - 1);
        int condStartIdx = posMap.getOrDefault(endRepeat.offset() - endRepeat.argument(), -1);
        if (condStartIdx < 1 || condStartIdx >= bytecodes.size()) return TAG_REPEAT_WHILE;

        Opcode getOp = switch (bytecodes.get(condStartIdx - 1).opcode()) {
            case SET_GLOBAL -> Opcode.GET_GLOBAL;
            case SET_GLOBAL2 -> Opcode.GET_GLOBAL2;
            case SET_PROP -> Opcode.GET_PROP;
            case SET_PARAM -> Opcode.GET_PARAM;
            case SET_LOCAL -> Opcode.GET_LOCAL;
            default -> null;
        };
        if (getOp == null) return TAG_REPEAT_WHILE;

        Opcode setOp = bytecodes.get(condStartIdx - 1).opcode();
        int varID = bytecodes.get(condStartIdx - 1).argument();

        if (bytecodes.get(condStartIdx).opcode() != getOp || bytecodes.get(condStartIdx).argument() != varID)
            return TAG_REPEAT_WHILE;

        if (endIdx < 5) return TAG_REPEAT_WHILE;
        int expectedInc = up ? 1 : -1;
        if (bytecodes.get(endIdx - 5).opcode() != Opcode.PUSH_INT8 || bytecodes.get(endIdx - 5).argument() != expectedInc)
            return TAG_REPEAT_WHILE;
        if (bytecodes.get(endIdx - 4).opcode() != getOp || bytecodes.get(endIdx - 4).argument() != varID)
            return TAG_REPEAT_WHILE;
        if (bytecodes.get(endIdx - 3).opcode() != Opcode.ADD)
            return TAG_REPEAT_WHILE;
        if (bytecodes.get(endIdx - 2).opcode() != setOp || bytecodes.get(endIdx - 2).argument() != varID)
            return TAG_REPEAT_WHILE;

        return up ? TAG_REPEAT_WITH_TO : TAG_REPEAT_WITH_DOWN_TO;
    }

    private boolean isRepeatWithIn(int startIdx, int endIdx) {
        if (startIdx < 7 || startIdx + 5 >= bytecodes.size()) return false;
        if (bytecodes.get(startIdx - 7).opcode() != Opcode.PEEK || bytecodes.get(startIdx - 7).argument() != 0) return false;
        if (bytecodes.get(startIdx - 6).opcode() != Opcode.PUSH_ARG_LIST || bytecodes.get(startIdx - 6).argument() != 1) return false;
        if (bytecodes.get(startIdx - 5).opcode() != Opcode.EXT_CALL || !resolveName(bytecodes.get(startIdx - 5).argument()).equals("count")) return false;
        if (bytecodes.get(startIdx - 4).opcode() != Opcode.PUSH_INT8 || bytecodes.get(startIdx - 4).argument() != 1) return false;
        if (bytecodes.get(startIdx - 3).opcode() != Opcode.PEEK || bytecodes.get(startIdx - 3).argument() != 0) return false;
        if (bytecodes.get(startIdx - 2).opcode() != Opcode.PEEK || bytecodes.get(startIdx - 2).argument() != 2) return false;
        if (bytecodes.get(startIdx - 1).opcode() != Opcode.LT_EQ) return false;
        if (bytecodes.get(startIdx + 1).opcode() != Opcode.PEEK || bytecodes.get(startIdx + 1).argument() != 2) return false;
        if (bytecodes.get(startIdx + 2).opcode() != Opcode.PEEK || bytecodes.get(startIdx + 2).argument() != 1) return false;
        if (bytecodes.get(startIdx + 3).opcode() != Opcode.PUSH_ARG_LIST || bytecodes.get(startIdx + 3).argument() != 2) return false;
        if (bytecodes.get(startIdx + 4).opcode() != Opcode.EXT_CALL || !resolveName(bytecodes.get(startIdx + 4).argument()).equals("getAt")) return false;
        var setOp = bytecodes.get(startIdx + 5).opcode();
        if (setOp != Opcode.SET_GLOBAL && setOp != Opcode.SET_PROP && setOp != Opcode.SET_PARAM && setOp != Opcode.SET_LOCAL) return false;
        if (endIdx < 3) return false;
        if (bytecodes.get(endIdx - 3).opcode() != Opcode.PUSH_INT8 || bytecodes.get(endIdx - 3).argument() != 1) return false;
        if (bytecodes.get(endIdx - 2).opcode() != Opcode.ADD) return false;
        if (endIdx >= bytecodes.size()) return false;
        if (bytecodes.get(endIdx).opcode() != Opcode.POP || bytecodes.get(endIdx).argument() != 3) return false;
        return true;
    }

    // ==================== Bytecode Translation ====================

    private int translateBytecode(ScriptChunk.Handler.Instruction bc, int index) {
        if (tags[index] == TAG_SKIP || tags[index] == TAG_NEXT_REPEAT_TARGET)
            return 1;

        LingoNode translation = null;
        BlockNode nextBlock = null;
        Opcode op = bc.opcode();
        int arg = bc.argument();

        switch (op) {
        case RET:
        case RET_FACTORY:
            if (index == bytecodes.size() - 1) return 1;
            translation = new ExitStmtNode();
            break;

        case PUSH_ZERO:
            translation = new LiteralNode(0);
            break;

        case MUL: case ADD: case SUB: case DIV: case MOD:
        case JOIN_STR: case JOIN_PAD_STR:
        case LT: case LT_EQ: case NT_EQ: case EQ: case GT: case GT_EQ:
        case AND: case OR: case CONTAINS_STR: case CONTAINS_0_STR: {
            var b = pop(); var a = pop();
            translation = new BinaryOpNode(op, a, b);
            break;
        }

        case INV:
            translation = new InverseOpNode(pop());
            break;

        case NOT:
            translation = new NotOpNode(pop());
            break;

        case GET_CHUNK:
            translation = readChunkRef(pop());
            break;

        case HILITE_CHUNK: {
            LingoNode castID = (version >= 500) ? pop() : null;
            var fieldID = pop();
            var field = new MemberExprNode("field", fieldID, castID);
            var chunk = readChunkRef(field);
            translation = (chunk instanceof CommentNode) ? chunk : new ChunkHiliteStmtNode(chunk);
            break;
        }

        case ONTO_SPR: {
            var b = pop(); var a = pop();
            translation = new SpriteIntersectsExprNode(a, b);
            break;
        }
        case INTO_SPR: {
            var b = pop(); var a = pop();
            translation = new SpriteWithinExprNode(a, b);
            break;
        }

        case GET_FIELD: {
            LingoNode castID = (version >= 500) ? pop() : null;
            translation = new MemberExprNode("field", pop(), castID);
            break;
        }

        case START_TELL: {
            var tell = new TellStmtNode(pop());
            translation = tell;
            nextBlock = tell.block;
            break;
        }
        case END_TELL:
            exitBlock();
            return 1;

        case PUSH_LIST: {
            var list = pop();
            list.setValueType(LingoNode.ValueType.LIST);
            translation = list;
            break;
        }
        case PUSH_PROP_LIST: {
            var list = pop();
            list.setValueType(LingoNode.ValueType.PROP_LIST);
            translation = list;
            break;
        }

        case SWAP:
            if (stack.size() >= 2) {
                int last = stack.size() - 1;
                var tmp = stack.get(last);
                stack.set(last, stack.get(last - 1));
                stack.set(last - 1, tmp);
            }
            return 1;

        case CALL_JAVASCRIPT: {
            stack.clear();
            String jsCode = "";
            if (!script.literals().isEmpty()) {
                var lit = script.literals().get(0);
                if (lit.value() instanceof String s) jsCode = s;
            }
            translation = new CommentNode("@js\n" + jsCode);
            break;
        }

        case PUSH_INT8: case PUSH_INT16: case PUSH_INT32:
            translation = new LiteralNode(arg);
            break;

        case PUSH_FLOAT32:
            translation = new LiteralNode((double) Float.intBitsToFloat(arg));
            break;

        case PUSH_ARG_LIST_NO_RET: {
            List<LingoNode> args = new ArrayList<>();
            for (int i = 0; i < arg; i++) args.add(0, pop());
            translation = new LiteralNode(LingoNode.ValueType.ARG_LIST_NO_RET, args);
            break;
        }
        case PUSH_ARG_LIST: {
            List<LingoNode> args = new ArrayList<>();
            for (int i = 0; i < arg; i++) args.add(0, pop());
            translation = new LiteralNode(LingoNode.ValueType.ARG_LIST, args);
            break;
        }

        case PUSH_CONS: {
            int litID = arg / variableMultiplier();
            if (litID >= 0 && litID < script.literals().size()) {
                var lit = script.literals().get(litID);
                translation = literalToNode(lit);
            } else {
                translation = new ErrorNode();
            }
            break;
        }

        case PUSH_SYMB:
            translation = new LiteralNode(LingoNode.ValueType.SYMBOL, resolveName(arg));
            break;

        case PUSH_VAR_REF:
            translation = new LiteralNode(LingoNode.ValueType.VAR_REF, resolveName(arg));
            break;

        case GET_GLOBAL: case GET_GLOBAL2:
            translation = new VarNode(resolveName(arg));
            break;

        case GET_PROP:
            translation = new VarNode(resolveName(arg));
            break;

        case GET_PARAM:
            translation = new VarNode(getArgumentName(arg));
            break;

        case GET_LOCAL:
            translation = new VarNode(getLocalName(arg));
            break;

        case SET_GLOBAL: case SET_GLOBAL2: {
            var var_ = new VarNode(resolveName(arg));
            translation = new AssignmentStmtNode(var_, pop());
            break;
        }
        case SET_PROP: {
            var var_ = new VarNode(resolveName(arg));
            translation = new AssignmentStmtNode(var_, pop());
            break;
        }
        case SET_PARAM: {
            var var_ = new VarNode(getArgumentName(arg));
            translation = new AssignmentStmtNode(var_, pop());
            break;
        }
        case SET_LOCAL: {
            var var_ = new VarNode(getLocalName(arg));
            translation = new AssignmentStmtNode(var_, pop());
            break;
        }

        case JMP: {
            int targetPos = bc.offset() + arg;
            Integer targetIdx = posMap.get(targetPos);
            if (targetIdx != null) {
                var ancestorLoop = currentBlock.ancestorLoop();
                if (ancestorLoop != null) {
                    if (targetIdx >= 1 && bytecodes.get(targetIdx - 1).opcode() == Opcode.END_REPEAT
                        && ownerLoops[targetIdx - 1] == ancestorLoop.startIndex) {
                        translation = new ExitRepeatStmtNode();
                        break;
                    }
                    if (tags[targetIdx] == TAG_NEXT_REPEAT_TARGET
                        && ownerLoops[targetIdx] == ancestorLoop.startIndex) {
                        translation = new NextRepeatStmtNode();
                        break;
                    }
                }
                // Check for if/else or case jump
                if (index + 1 < bytecodes.size()) {
                    var nextBc = bytecodes.get(index + 1);
                    var ancestorStmt = currentBlock.ancestorStatement();
                    if (ancestorStmt != null && nextBc.offset() == currentBlock.endPos) {
                        if (ancestorStmt instanceof IfStmtNode ifStmt) {
                            if (currentBlock == ifStmt.block1) {
                                ifStmt.hasElse = true;
                                ifStmt.block2.endPos = targetPos;
                                return 1;
                            }
                        } else if (ancestorStmt instanceof CasesStmtNode casesStmt) {
                            casesStmt.endPos = targetPos;
                            return 1;
                        }
                    }
                }
            }
            translation = new CommentNode("ERROR: Could not identify jmp");
            break;
        }

        case END_REPEAT:
            translation = new CommentNode("ERROR: Stray endrepeat");
            break;

        case JMP_IF_Z: {
            int endPos = bc.offset() + arg;
            switch (tags[index]) {
            case TAG_REPEAT_WHILE: {
                var cond = pop();
                var loop = new RepeatWhileStmtNode(index, cond);
                loop.block.endPos = endPos;
                translation = loop;
                nextBlock = loop.block;
                break;
            }
            case TAG_REPEAT_WITH_IN: {
                var list = pop();
                String varName = getVarNameFromSet(bytecodes.get(index + 5));
                var loop = new RepeatWithInStmtNode(index, varName, list);
                loop.block.endPos = endPos;
                translation = loop;
                nextBlock = loop.block;
                break;
            }
            case TAG_REPEAT_WITH_TO: case TAG_REPEAT_WITH_DOWN_TO: {
                boolean up = (tags[index] == TAG_REPEAT_WITH_TO);
                var end = pop(); var start = pop();
                Integer endIdxObj = posMap.get(endPos);
                if (endIdxObj != null && endIdxObj >= 1) {
                    var endRepeat = bytecodes.get(endIdxObj - 1);
                    int condStart = posMap.getOrDefault(endRepeat.offset() - endRepeat.argument(), -1);
                    if (condStart >= 1) {
                        String varName = getVarNameFromSet(bytecodes.get(condStart - 1));
                        var loop = new RepeatWithToStmtNode(index, varName, start, up, end);
                        loop.block.endPos = endPos;
                        translation = loop;
                        nextBlock = loop.block;
                        break;
                    }
                }
                translation = new CommentNode("ERROR: Could not identify repeat with to");
                break;
            }
            default: {
                var cond = pop();
                var ifStmt = new IfStmtNode(cond);
                ifStmt.block1.endPos = endPos;
                translation = ifStmt;
                nextBlock = ifStmt.block1;
                break;
            }}
            break;
        }

        case LOCAL_CALL: {
            var argList = pop();
            String callName;
            if (arg >= 0 && arg < script.handlers().size()) {
                callName = resolveName(script.handlers().get(arg).nameId());
            } else {
                callName = "handler#" + arg;
            }
            translation = new CallNode(callName, argList);
            break;
        }

        case EXT_CALL: case TELL_CALL: {
            var argList = pop();
            translation = new CallNode(resolveName(arg), argList);
            break;
        }

        case OBJ_CALL_V4: {
            var object = readVar(arg);
            var argList = pop();
            var rawArgs = argList.getArgNodes();
            if (!rawArgs.isEmpty()) {
                // First arg is a symbol - replace with var
                var first = rawArgs.get(0);
                if (rawArgs instanceof ArrayList<LingoNode> mutableArgs) {
                    mutableArgs.set(0, new VarNode(first.getStringValue()));
                }
            }
            translation = new ObjCallV4Node(object, argList);
            break;
        }

        case PUT: {
            int putType = (arg >> 4) & 0xF;
            int varType = arg & 0xF;
            var var_ = readVar(varType);
            var val = pop();
            translation = new PutStmtNode(putType, var_, val);
            break;
        }

        case PUT_CHUNK: {
            int putType = (arg >> 4) & 0xF;
            int varType = arg & 0xF;
            var var_ = readVar(varType);
            var chunk = readChunkRef(var_);
            var val = pop();
            translation = (chunk instanceof CommentNode) ? chunk : new PutStmtNode(putType, chunk, val);
            break;
        }

        case DELETE_CHUNK: {
            var var_ = readVar(arg);
            var chunk = readChunkRef(var_);
            translation = (chunk instanceof CommentNode) ? chunk : new ChunkDeleteStmtNode(chunk);
            break;
        }

        case GET: {
            int propID = pop().getIntValue();
            translation = readV4Property(arg, propID);
            break;
        }

        case SET: {
            int propID = pop().getIntValue();
            var value = pop();
            if (arg == 0x00 && propID >= 0x01 && propID <= 0x05
                && value.getValueType() == LingoNode.ValueType.STRING) {
                String scr = value.getStringValue();
                if (!scr.isEmpty() && (scr.charAt(0) == ' ' || scr.indexOf('\r') >= 0)) {
                    translation = new WhenStmtNode(propID, scr);
                }
            }
            if (translation == null) {
                var prop = readV4Property(arg, propID);
                if (prop instanceof CommentNode)
                    translation = prop;
                else
                    translation = new AssignmentStmtNode(prop, value, true);
            }
            break;
        }

        case GET_MOVIE_PROP:
            translation = new TheExprNode(resolveName(arg));
            break;

        case SET_MOVIE_PROP: {
            var value = pop();
            var prop = new TheExprNode(resolveName(arg));
            translation = new AssignmentStmtNode(prop, value);
            break;
        }

        case GET_OBJ_PROP: case GET_CHAINED_PROP: {
            var object = pop();
            translation = new ObjPropExprNode(object, resolveName(arg));
            break;
        }

        case SET_OBJ_PROP: {
            var value = pop();
            var object = pop();
            var prop = new ObjPropExprNode(object, resolveName(arg));
            translation = new AssignmentStmtNode(prop, value);
            break;
        }

        case PEEK:
            return translatePeek(bc, index);

        case POP:
            for (int i = 0; i < arg; i++) pop();
            return 1;

        case THE_BUILTIN: {
            pop(); // empty arglist
            translation = new TheExprNode(resolveName(arg));
            break;
        }

        case OBJ_CALL:
            return translateObjCall(arg);

        case PUSH_CHUNK_VAR_REF:
            translation = readVar(arg);
            break;

        case GET_TOP_LEVEL_PROP:
            translation = new VarNode(resolveName(arg));
            break;

        case NEW_OBJ: {
            var objArgs = pop();
            translation = new NewObjNode(resolveName(arg), objArgs);
            break;
        }

        default: {
            String text = op.getMnemonic();
            if (bc.rawOpcode() >= 0x40) text += " " + arg;
            translation = new CommentNode(text);
            stack.clear();
            break;
        }}

        if (translation == null)
            translation = new ErrorNode();

        if (translation.isExpression) {
            stack.add(translation);
        } else {
            addStatement(translation);
        }

        if (nextBlock != null) enterBlock(nextBlock);

        return 1;
    }

    // ==================== ObjCall Translation ====================

    private int translateObjCall(int arg) {
        String method = resolveName(arg);
        var argList = pop();
        var rawArgs = argList.getArgNodes();
        int nargs = rawArgs.size();

        LingoNode translation;

        if (method.equals("getAt") && nargs == 2) {
            translation = new ObjBracketExprNode(rawArgs.get(0), rawArgs.get(1));
        } else if (method.equals("setAt") && nargs == 3) {
            var propExpr = new ObjBracketExprNode(rawArgs.get(0), rawArgs.get(1));
            translation = new AssignmentStmtNode(propExpr, rawArgs.get(2));
        } else if ((method.equals("getProp") || method.equals("getPropRef"))
            && (nargs == 3 || nargs == 4)
            && rawArgs.get(1).getValueType() == LingoNode.ValueType.SYMBOL) {
            String propName = rawArgs.get(1).getStringValue();
            var i2 = (nargs == 4) ? rawArgs.get(3) : null;
            translation = new ObjPropIndexExprNode(rawArgs.get(0), propName, rawArgs.get(2), i2);
        } else if (method.equals("setProp") && (nargs == 4 || nargs == 5)
            && rawArgs.get(1).getValueType() == LingoNode.ValueType.SYMBOL) {
            String propName = rawArgs.get(1).getStringValue();
            var i2 = (nargs == 5) ? rawArgs.get(3) : null;
            var propExpr = new ObjPropIndexExprNode(rawArgs.get(0), propName, rawArgs.get(2), i2);
            translation = new AssignmentStmtNode(propExpr, rawArgs.get(nargs - 1));
        } else if (method.equals("count") && nargs == 2
            && rawArgs.get(1).getValueType() == LingoNode.ValueType.SYMBOL) {
            String propName = rawArgs.get(1).getStringValue();
            var propExpr = new ObjPropExprNode(rawArgs.get(0), propName);
            translation = new ObjPropExprNode(propExpr, "count");
        } else if ((method.equals("setContents") || method.equals("setContentsAfter") || method.equals("setContentsBefore"))
            && nargs == 2) {
            int putType = method.equals("setContents") ? 1 : method.equals("setContentsAfter") ? 2 : 3;
            translation = new PutStmtNode(putType, rawArgs.get(0), rawArgs.get(1));
        } else if (method.equals("hilite") && nargs == 1) {
            translation = new ChunkHiliteStmtNode(rawArgs.get(0));
        } else if (method.equals("delete") && nargs == 1) {
            translation = new ChunkDeleteStmtNode(rawArgs.get(0));
        } else {
            translation = new ObjCallNode(method, argList);
        }

        if (translation.isExpression)
            stack.add(translation);
        else
            addStatement(translation);
        return 1;
    }

    // ==================== Peek / Case Translation ====================

    private int translatePeek(ScriptChunk.Handler.Instruction bc, int index) {
        var peekedValue = peek();
        var prevCase = currentBlock.currentCase;

        // Translate sub-bytecodes to find the case comparison
        // Matches ProjectorRays: translate, advance, then check if next is eq/nteq
        int origStackSize = stack.size();
        int currIdx = index + 1;
        while (currIdx < bytecodes.size()) {
            translateBytecode(bytecodes.get(currIdx), currIdx);
            currIdx++;
            if (currIdx < bytecodes.size()
                && stack.size() == origStackSize + 1) {
                var nextOp = bytecodes.get(currIdx).opcode();
                if (nextOp == Opcode.EQ || nextOp == Opcode.NT_EQ)
                    break;
            }
        }

        if (currIdx >= bytecodes.size()) {
            addStatement(new CommentNode("ERROR: Expected eq or nteq!"));
            return currIdx - index + 1;
        }

        boolean notEq = bytecodes.get(currIdx).opcode() == Opcode.NT_EQ;
        var caseValue = pop();
        currIdx++;

        if (currIdx >= bytecodes.size() || bytecodes.get(currIdx).opcode() != Opcode.JMP_IF_Z) {
            addStatement(new CommentNode("ERROR: Expected jmpifz!"));
            return currIdx - index + 1;
        }

        var jmpifz = bytecodes.get(currIdx);
        int jmpPos = jmpifz.offset() + jmpifz.argument();
        Integer targetIdxObj = posMap.get(jmpPos);
        int targetIdx = targetIdxObj != null ? targetIdxObj : 0;

        int expect;
        if (notEq)
            expect = CaseNode.EXPECT_OR;
        else if (targetIdx < bytecodes.size() && bytecodes.get(targetIdx).opcode() == Opcode.PEEK)
            expect = CaseNode.EXPECT_NEXT;
        else if (targetIdx < bytecodes.size() && bytecodes.get(targetIdx).opcode() == Opcode.POP)
            expect = CaseNode.EXPECT_POP;
        else
            expect = CaseNode.EXPECT_OTHERWISE;

        var currCase = new CaseNode(caseValue, expect);
        currentBlock.currentCase = currCase;

        if (prevCase == null) {
            var casesStmt = new CasesStmtNode(peekedValue);
            casesStmt.firstCase = currCase;
            currCase.parent = casesStmt;
            addStatement(casesStmt);
        } else if (prevCase.expect == CaseNode.EXPECT_OR) {
            prevCase.nextOr = currCase;
            currCase.parent = prevCase;
        } else if (prevCase.expect == CaseNode.EXPECT_NEXT) {
            prevCase.nextCase = currCase;
            currCase.parent = prevCase;
        }

        if (expect != CaseNode.EXPECT_OR) {
            currCase.block = new BlockNode();
            currCase.block.parent = currCase;
            currCase.block.endPos = jmpPos;
            enterBlock(currCase.block);
        }

        return currIdx - index + 1;
    }

    // ==================== V4 Property Reading ====================

    private LingoNode readV4Property(int propertyType, int propertyID) {
        switch (propertyType) {
        case 0x00:
            if (propertyID <= 0x0b) {
                return new TheExprNode(getName(MOVIE_PROPERTY_NAMES, propertyID));
            } else {
                var string = pop();
                int chunkType = propertyID - 0x0b;
                return new LastStringChunkExprNode(chunkType, string);
            }
        case 0x01: {
            var string = pop();
            return new StringChunkCountExprNode(propertyID, string);
        }
        case 0x02: return new MenuPropExprNode(pop(), propertyID);
        case 0x03: { var m = pop(); return new MenuItemPropExprNode(m, pop(), propertyID); }
        case 0x04: return new SoundPropExprNode(pop(), propertyID);
        case 0x05: return new CommentNode("ERROR: Resource property");
        case 0x06: return new SpritePropExprNode(pop(), propertyID);
        case 0x07: return new TheExprNode(getName(ANIMATION_PROPERTY_NAMES, propertyID));
        case 0x08:
            if (propertyID == 0x02 && version >= 500) {
                var castLib = pop();
                if (!(castLib instanceof LiteralNode ln && ln.getValueType() == LingoNode.ValueType.INT && ln.intVal == 0)) {
                    var castLibNode = new MemberExprNode("castLib", castLib, null);
                    return new ThePropExprNode(castLibNode, getName(ANIMATION2_PROPERTY_NAMES, propertyID));
                }
            }
            return new TheExprNode(getName(ANIMATION2_PROPERTY_NAMES, propertyID));
        case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
        case 0x0e: case 0x0f: case 0x10: case 0x11: case 0x12:
        case 0x13: case 0x14: case 0x15: {
            var propName = getName(MEMBER_PROPERTY_NAMES, propertyID);
            LingoNode castID = (version >= 500) ? pop() : null;
            var memberID = pop();
            String prefix;
            if (propertyType == 0x0b || propertyType == 0x0c) prefix = "field";
            else if (propertyType == 0x14 || propertyType == 0x15) prefix = "script";
            else prefix = (version >= 500) ? "member" : "cast";
            var member = new MemberExprNode(prefix, memberID, castID);
            LingoNode entity;
            if (propertyType == 0x0a || propertyType == 0x0c || propertyType == 0x15) {
                entity = readChunkRef(member);
            } else {
                entity = member;
            }
            return new ThePropExprNode(entity, propName);
        }
        default:
            return new CommentNode("ERROR: Unknown property type " + propertyType);
        }
    }

    // ==================== Variable Reading ====================

    private LingoNode readVar(int varType) {
        LingoNode castID = null;
        if (varType == 0x6 && version >= 500)
            castID = pop();
        var id = pop();

        return switch (varType) {
            case 0x1, 0x2, 0x3 -> id;
            case 0x4 -> {
                String name = getArgumentName(id.getIntValue());
                yield new LiteralNode(LingoNode.ValueType.VAR_REF, name);
            }
            case 0x5 -> {
                String name = getLocalName(id.getIntValue());
                yield new LiteralNode(LingoNode.ValueType.VAR_REF, name);
            }
            case 0x6 -> new MemberExprNode("field", id, castID);
            default -> new ErrorNode();
        };
    }

    private LingoNode readChunkRef(LingoNode string) {
        var lastLine = pop(); var firstLine = pop();
        var lastItem = pop(); var firstItem = pop();
        var lastWord = pop(); var firstWord = pop();
        var lastChar = pop(); var firstChar = pop();

        if (!isZeroLiteral(firstLine))
            string = new ChunkExprNode(4, firstLine, lastLine, string);
        if (!isZeroLiteral(firstItem))
            string = new ChunkExprNode(3, firstItem, lastItem, string);
        if (!isZeroLiteral(firstWord))
            string = new ChunkExprNode(2, firstWord, lastWord, string);
        if (!isZeroLiteral(firstChar))
            string = new ChunkExprNode(1, firstChar, lastChar, string);

        return string;
    }

    private String getVarNameFromSet(ScriptChunk.Handler.Instruction bc) {
        return switch (bc.opcode()) {
            case SET_GLOBAL, SET_GLOBAL2, SET_PROP -> resolveName(bc.argument());
            case SET_PARAM -> getArgumentName(bc.argument());
            case SET_LOCAL -> getLocalName(bc.argument());
            default -> "ERROR";
        };
    }

    // ==================== Stack / AST Helpers ====================

    private LingoNode pop() {
        if (stack.isEmpty()) return new ErrorNode();
        return stack.remove(stack.size() - 1);
    }

    private LingoNode peek() {
        if (stack.isEmpty()) return new ErrorNode();
        return stack.get(stack.size() - 1);
    }

    private void addStatement(LingoNode stmt) {
        currentBlock.addChild(stmt);
    }

    private void enterBlock(BlockNode block) {
        currentBlock = block;
    }

    private void exitBlock() {
        var ancestorStmt = currentBlock.ancestorStatement();
        if (ancestorStmt == null) { currentBlock = null; return; }
        var block = ancestorStmt.parent;
        if (block instanceof BlockNode bn) {
            currentBlock = bn;
        } else {
            currentBlock = null;
        }
    }

    // ==================== Name Resolution ====================

    private String resolveName(int nameId) {
        if (names != null && nameId >= 0 && nameId < names.names().size())
            return names.getName(nameId);
        return "#" + nameId;
    }

    private int variableMultiplier() {
        if (capitalX) return 1;
        if (version >= 500) return 8;
        return 6;
    }

    private String getArgumentName(int rawIndex) {
        int idx = rawIndex / variableMultiplier();
        if (currentHandler != null && idx >= 0 && idx < currentHandler.argNameIds().size()) {
            return resolveName(currentHandler.argNameIds().get(idx));
        }
        return "UNKNOWN_ARG_" + idx;
    }

    private String getLocalName(int rawIndex) {
        int idx = rawIndex / variableMultiplier();
        if (currentHandler != null && idx >= 0 && idx < currentHandler.localNameIds().size()) {
            return resolveName(currentHandler.localNameIds().get(idx));
        }
        return "UNKNOWN_LOCAL_" + idx;
    }

    private LingoNode literalToNode(ScriptChunk.LiteralEntry lit) {
        return switch (lit.type()) {
            case 1 -> { // String
                String s = lit.value() instanceof String sv ? sv : String.valueOf(lit.value());
                yield new LiteralNode(LingoNode.ValueType.STRING, s);
            }
            case 4 -> // Int
                new LiteralNode(lit.value() instanceof Integer iv ? iv : 0);
            case 9 -> // Float
                new LiteralNode(lit.numericValue());
            default -> new LiteralNode(LingoNode.ValueType.STRING, String.valueOf(lit.value()));
        };
    }

    private static boolean isZeroLiteral(LingoNode node) {
        return node instanceof LiteralNode ln
            && ln.getValueType() == LingoNode.ValueType.INT
            && ln.intVal == 0;
    }

    // ==================== Fallback bytecode-only display ====================

    private String formatHandlerBytecodeOnly(ScriptChunk.Handler handler) {
        StringBuilder sb = new StringBuilder();
        String name = resolveName(handler.nameId());
        sb.append("on ").append(name).append("\n");
        for (var instr : handler.instructions()) {
            sb.append(String.format("  [%04d] %-16s", instr.offset(), instr.opcode().getMnemonic()));
            if (instr.rawOpcode() >= 0x40) sb.append(" ").append(instr.argument());
            sb.append("\n");
        }
        sb.append("end\n");
        return sb.toString();
    }
}
