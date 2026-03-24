package com.libreshockwave.lingo.decompiler;

import com.libreshockwave.lingo.Opcode;

import java.util.ArrayList;
import java.util.List;

import static com.libreshockwave.lingo.decompiler.LingoProperties.*;

/**
 * AST node hierarchy for Lingo decompilation.
 * Ported from ProjectorRays lingo.h / lingo.cpp.
 *
 * Each node can produce reconstructed Lingo source via {@link #toLingo(boolean)}.
 */
public abstract class LingoNode {

    // Datum-like value types for the decompiler's intermediate representation
    public enum ValueType {
        VOID, SYMBOL, VAR_REF, STRING, INT, FLOAT,
        LIST, ARG_LIST, ARG_LIST_NO_RET, PROP_LIST
    }

    LingoNode parent;
    boolean isExpression;
    boolean isStatement;
    boolean isLoop;

    protected LingoNode() {}

    /** Produce Lingo source text. @param dot true for dot syntax (Director 7+) */
    public abstract String toLingo(boolean dot);

    // Value accessors for decompiler use (overridden by LiteralNode)
    public ValueType getValueType() { return ValueType.VOID; }
    public int getIntValue() { return 0; }
    public String getStringValue() { return ""; }
    public List<LingoNode> getArgNodes() { return List.of(); }
    public void setValueType(ValueType t) {} // for list/proplist mutation

    LingoNode ancestorStatement() {
        LingoNode a = parent;
        while (a != null && !a.isStatement) a = a.parent;
        return a;
    }

    LoopNodeBase ancestorLoop() {
        LingoNode a = parent;
        while (a != null && !a.isLoop) a = a.parent;
        return (a instanceof LoopNodeBase l) ? l : null;
    }

    static String indent(String text) {
        StringBuilder sb = new StringBuilder();
        for (String line : text.split("\n", -1)) {
            if (!line.isEmpty()) {
                sb.append("  ").append(line);
            }
            sb.append("\n");
        }
        // Remove trailing extra newline if present
        if (sb.length() > 0 && sb.charAt(sb.length() - 1) == '\n'
                && text.length() > 0 && text.charAt(text.length() - 1) != '\n') {
            sb.setLength(sb.length() - 1);
        }
        return sb.toString();
    }

    // ==================== Expression Nodes ====================

    public static class ErrorNode extends LingoNode {
        { isExpression = true; }
        @Override public String toLingo(boolean dot) { return "ERROR"; }
    }

    public static class CommentNode extends LingoNode {
        public final String text;
        public CommentNode(String t) { this.text = t; }
        @Override public String toLingo(boolean dot) { return "-- " + text; }
    }

    public static class LiteralNode extends LingoNode {
        private ValueType valueType;
        public int intVal;
        public double floatVal;
        public String strVal;
        public final List<LingoNode> listItems;

        { isExpression = true; }

        // Int
        public LiteralNode(int val) {
            valueType = ValueType.INT; intVal = val; strVal = ""; listItems = List.of();
        }
        // Float
        public LiteralNode(double val) {
            valueType = ValueType.FLOAT; floatVal = val; strVal = ""; listItems = List.of();
        }
        // String/Symbol/VarRef
        public LiteralNode(ValueType type, String val) {
            valueType = type; strVal = val != null ? val : ""; listItems = List.of();
        }
        // List/ArgList/PropList
        public LiteralNode(ValueType type, List<LingoNode> items) {
            valueType = type; strVal = ""; listItems = items;
            for (LingoNode item : items) item.parent = this;
        }

        @Override public ValueType getValueType() { return valueType; }
        @Override public int getIntValue() { return intVal; }
        @Override public String getStringValue() { return strVal; }
        @Override public List<LingoNode> getArgNodes() { return listItems; }
        @Override public void setValueType(ValueType t) { this.valueType = t; }

        @Override
        public String toLingo(boolean dot) {
            return switch (valueType) {
                case VOID -> "VOID";
                case SYMBOL -> "#" + strVal;
                case VAR_REF -> strVal;
                case STRING -> {
                    if (strVal.isEmpty()) yield "EMPTY";
                    if (strVal.length() == 1) {
                        yield switch (strVal.charAt(0)) {
                            case '\u0003' -> "ENTER";
                            case '\b' -> "BACKSPACE";
                            case '\t' -> "TAB";
                            case '\r' -> "RETURN";
                            case '"' -> "QUOTE";
                            default -> "\"" + strVal + "\"";
                        };
                    }
                    yield "\"" + strVal + "\"";
                }
                case INT -> String.valueOf(intVal);
                case FLOAT -> formatFloat(floatVal);
                case LIST -> {
                    StringBuilder sb = new StringBuilder("[");
                    for (int i = 0; i < listItems.size(); i++) {
                        if (i > 0) sb.append(", ");
                        sb.append(listItems.get(i).toLingo(dot));
                    }
                    sb.append("]");
                    yield sb.toString();
                }
                case ARG_LIST, ARG_LIST_NO_RET -> {
                    StringBuilder sb = new StringBuilder();
                    for (int i = 0; i < listItems.size(); i++) {
                        if (i > 0) sb.append(", ");
                        sb.append(listItems.get(i).toLingo(dot));
                    }
                    yield sb.toString();
                }
                case PROP_LIST -> {
                    StringBuilder sb = new StringBuilder("[");
                    if (listItems.isEmpty()) {
                        sb.append(":");
                    } else {
                        for (int i = 0; i < listItems.size(); i += 2) {
                            if (i > 0) sb.append(", ");
                            sb.append(listItems.get(i).toLingo(dot));
                            sb.append(": ");
                            if (i + 1 < listItems.size())
                                sb.append(listItems.get(i + 1).toLingo(dot));
                        }
                    }
                    sb.append("]");
                    yield sb.toString();
                }
            };
        }

        private static String formatFloat(double f) {
            String s = String.valueOf(f);
            // Remove unnecessary trailing zeros but keep at least one decimal
            if (s.contains(".")) {
                s = s.replaceAll("0+$", "");
                if (s.endsWith(".")) s += "0";
            }
            return s;
        }
    }

    public static class VarNode extends LingoNode {
        public final String varName;
        { isExpression = true; }
        public VarNode(String name) { this.varName = name; }
        @Override public String toLingo(boolean dot) { return varName; }
    }

    public static class InverseOpNode extends LingoNode {
        public final LingoNode operand;
        { isExpression = true; }
        public InverseOpNode(LingoNode op) {
            this.operand = op; op.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            if (operand instanceof BinaryOpNode)
                return "-(" + operand.toLingo(dot) + ")";
            return "-" + operand.toLingo(dot);
        }
    }

    public static class NotOpNode extends LingoNode {
        public final LingoNode operand;
        { isExpression = true; }
        public NotOpNode(LingoNode op) {
            this.operand = op; op.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            if (operand instanceof BinaryOpNode)
                return "not (" + operand.toLingo(dot) + ")";
            return "not " + operand.toLingo(dot);
        }
    }

    public static class BinaryOpNode extends LingoNode {
        public final Opcode opcode;
        public final LingoNode left, right;
        { isExpression = true; }

        public BinaryOpNode(Opcode op, LingoNode a, LingoNode b) {
            this.opcode = op; this.left = a; this.right = b;
            a.parent = this; b.parent = this;
        }

        int getPrecedence() {
            return switch (opcode) {
                case MUL, DIV, MOD -> 1;
                case ADD, SUB -> 2;
                case LT, LT_EQ, NT_EQ, EQ, GT, GT_EQ -> 3;
                case AND -> 4;
                case OR -> 5;
                default -> 0;
            };
        }

        @Override public String toLingo(boolean dot) {
            String opStr = BINARY_OP_NAMES.getOrDefault(opcode, "?");
            String ls = left.toLingo(dot);
            String rs = right.toLingo(dot);
            int prec = getPrecedence();
            if (prec > 0) {
                if (left instanceof BinaryOpNode lb && lb.getPrecedence() > prec)
                    ls = "(" + ls + ")";
                if (right instanceof BinaryOpNode rb && rb.getPrecedence() >= prec)
                    rs = "(" + rs + ")";
            }
            return ls + " " + opStr + " " + rs;
        }
    }

    public static class TheExprNode extends LingoNode {
        public final String prop;
        { isExpression = true; }
        public TheExprNode(String p) { this.prop = p; }
        @Override public String toLingo(boolean dot) { return "the " + prop; }
    }

    public static class MemberExprNode extends LingoNode {
        public final String memberType;
        public final LingoNode memberID;
        public final LingoNode castID; // may be null
        { isExpression = true; }

        public MemberExprNode(String type, LingoNode id, LingoNode cast) {
            this.memberType = type; this.memberID = id; this.castID = cast;
            id.parent = this;
            if (cast != null) cast.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            boolean noCast = castID == null ||
                (castID instanceof LiteralNode ln && ln.getValueType() == ValueType.INT && ln.intVal == 0);
            if (noCast) {
                if (dot)
                    return memberType + "(" + memberID.toLingo(dot) + ")";
                if (memberID instanceof BinaryOpNode)
                    return memberType + " (" + memberID.toLingo(dot) + ")";
                return memberType + " " + memberID.toLingo(dot);
            }
            return memberType + "(" + memberID.toLingo(dot) + ", " + castID.toLingo(dot) + ")";
        }
    }

    public static class ObjPropExprNode extends LingoNode {
        public final LingoNode obj;
        public final String prop;
        { isExpression = true; }

        public ObjPropExprNode(LingoNode o, String p) {
            this.obj = o; this.prop = p; o.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            if (dot) {
                String left = maybeParenDot(obj, dot);
                return left + "." + prop;
            }
            return "the " + prop + " of " + obj.toLingo(dot);
        }
    }

    public static class ObjBracketExprNode extends LingoNode {
        public final LingoNode obj, prop;
        { isExpression = true; }
        public ObjBracketExprNode(LingoNode o, LingoNode p) {
            this.obj = o; this.prop = p; o.parent = this; p.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return obj.toLingo(dot) + "[" + prop.toLingo(dot) + "]";
        }
    }

    public static class ObjPropIndexExprNode extends LingoNode {
        public final LingoNode obj;
        public final String prop;
        public final LingoNode index, index2;
        { isExpression = true; }

        public ObjPropIndexExprNode(LingoNode o, String p, LingoNode i, LingoNode i2) {
            this.obj = o; this.prop = p; this.index = i; this.index2 = i2;
            o.parent = this; i.parent = this;
            if (i2 != null) i2.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            String left = maybeParenDot(obj, dot);
            String res = left + "." + prop + "[" + index.toLingo(dot);
            if (index2 != null) res += ".." + index2.toLingo(dot);
            return res + "]";
        }
    }

    public static class ThePropExprNode extends LingoNode {
        public final LingoNode obj;
        public final String prop;
        { isExpression = true; }

        public ThePropExprNode(LingoNode o, String p) {
            this.obj = o; this.prop = p; o.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "the " + prop + " of " + obj.toLingo(false);
        }
    }

    public static class ChunkExprNode extends LingoNode {
        public final int chunkType; // 1=char, 2=word, 3=item, 4=line
        public final LingoNode first, last, string;
        { isExpression = true; }

        public ChunkExprNode(int type, LingoNode f, LingoNode l, LingoNode s) {
            this.chunkType = type; this.first = f; this.last = l; this.string = s;
            f.parent = this; l.parent = this; s.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            String typeName = CHUNK_TYPE_NAMES.getOrDefault(chunkType, "chunk");
            String res = typeName + " " + first.toLingo(dot);
            if (!(last instanceof LiteralNode ln && ln.getValueType() == ValueType.INT && ln.intVal == 0)) {
                res += " to " + last.toLingo(dot);
            }
            res += " of " + string.toLingo(false);
            return res;
        }
    }

    public static class LastStringChunkExprNode extends LingoNode {
        public final int chunkType;
        public final LingoNode string;
        { isExpression = true; }

        public LastStringChunkExprNode(int type, LingoNode s) {
            this.chunkType = type; this.string = s; s.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "the last " + CHUNK_TYPE_NAMES.getOrDefault(chunkType, "chunk")
                + " in " + string.toLingo(false);
        }
    }

    public static class StringChunkCountExprNode extends LingoNode {
        public final int chunkType;
        public final LingoNode string;
        { isExpression = true; }

        public StringChunkCountExprNode(int type, LingoNode s) {
            this.chunkType = type; this.string = s; s.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "the number of " + CHUNK_TYPE_NAMES.getOrDefault(chunkType, "chunk")
                + "s in " + string.toLingo(false);
        }
    }

    public static class SpriteIntersectsExprNode extends LingoNode {
        public final LingoNode first, second;
        { isExpression = true; }
        public SpriteIntersectsExprNode(LingoNode a, LingoNode b) {
            first = a; second = b; a.parent = this; b.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "sprite " + first.toLingo(dot) + " intersects " + second.toLingo(dot);
        }
    }

    public static class SpriteWithinExprNode extends LingoNode {
        public final LingoNode first, second;
        { isExpression = true; }
        public SpriteWithinExprNode(LingoNode a, LingoNode b) {
            first = a; second = b; a.parent = this; b.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "sprite " + first.toLingo(dot) + " within " + second.toLingo(dot);
        }
    }

    public static class MenuPropExprNode extends LingoNode {
        public final LingoNode menuID;
        public final int prop;
        { isExpression = true; }
        public MenuPropExprNode(LingoNode m, int p) {
            menuID = m; prop = p; m.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "the " + getName(MENU_PROPERTY_NAMES, prop) + " of menu " + menuID.toLingo(dot);
        }
    }

    public static class MenuItemPropExprNode extends LingoNode {
        public final LingoNode menuID, itemID;
        public final int prop;
        { isExpression = true; }
        public MenuItemPropExprNode(LingoNode m, LingoNode i, int p) {
            menuID = m; itemID = i; prop = p; m.parent = this; i.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "the " + getName(MENU_ITEM_PROPERTY_NAMES, prop)
                + " of menuItem " + itemID.toLingo(dot) + " of menu " + menuID.toLingo(dot);
        }
    }

    public static class SoundPropExprNode extends LingoNode {
        public final LingoNode soundID;
        public final int prop;
        { isExpression = true; }
        public SoundPropExprNode(LingoNode s, int p) {
            soundID = s; prop = p; s.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "the " + getName(SOUND_PROPERTY_NAMES, prop) + " of sound " + soundID.toLingo(dot);
        }
    }

    public static class SpritePropExprNode extends LingoNode {
        public final LingoNode spriteID;
        public final int prop;
        { isExpression = true; }
        public SpritePropExprNode(LingoNode s, int p) {
            spriteID = s; prop = p; s.parent = this;
        }
        @Override public String toLingo(boolean dot) {
            return "the " + getName(SPRITE_PROPERTY_NAMES, prop) + " of sprite " + spriteID.toLingo(dot);
        }
    }

    public static class NewObjNode extends LingoNode {
        public final String objType;
        public final LingoNode objArgs;
        { isExpression = true; }
        public NewObjNode(String type, LingoNode args) {
            this.objType = type; this.objArgs = args;
        }
        @Override public String toLingo(boolean dot) {
            return "new " + objType + "(" + objArgs.toLingo(dot) + ")";
        }
    }

    // ==================== Statement Nodes ====================

    public static class ExitStmtNode extends LingoNode {
        { isStatement = true; }
        @Override public String toLingo(boolean dot) { return "exit"; }
    }

    public static class ExitRepeatStmtNode extends LingoNode {
        { isStatement = true; }
        @Override public String toLingo(boolean dot) { return "exit repeat"; }
    }

    public static class NextRepeatStmtNode extends LingoNode {
        { isStatement = true; }
        @Override public String toLingo(boolean dot) { return "next repeat"; }
    }

    public static class AssignmentStmtNode extends LingoNode {
        public final LingoNode variable, value;
        public final boolean forceVerbose;
        { isStatement = true; }

        public AssignmentStmtNode(LingoNode var, LingoNode val, boolean forceVerbose) {
            this.variable = var; this.value = val; this.forceVerbose = forceVerbose;
            var.parent = this; val.parent = this;
        }

        public AssignmentStmtNode(LingoNode var, LingoNode val) {
            this(var, val, false);
        }

        @Override public String toLingo(boolean dot) {
            if (!dot || forceVerbose)
                return "set " + variable.toLingo(false) + " to " + value.toLingo(dot);
            return variable.toLingo(dot) + " = " + value.toLingo(dot);
        }
    }

    public static class PutStmtNode extends LingoNode {
        public final int putType; // 1=into, 2=after, 3=before
        public final LingoNode variable, value;
        { isStatement = true; }

        public PutStmtNode(int type, LingoNode var, LingoNode val) {
            this.putType = type; this.variable = var; this.value = val;
            var.parent = this; val.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            String typeStr = PUT_TYPE_NAMES.getOrDefault(putType, "into");
            return "put " + value.toLingo(dot) + " " + typeStr + " " + variable.toLingo(false);
        }
    }

    public static class ChunkHiliteStmtNode extends LingoNode {
        public final LingoNode chunk;
        { isStatement = true; }
        public ChunkHiliteStmtNode(LingoNode c) { chunk = c; c.parent = this; }
        @Override public String toLingo(boolean dot) { return "hilite " + chunk.toLingo(dot); }
    }

    public static class ChunkDeleteStmtNode extends LingoNode {
        public final LingoNode chunk;
        { isStatement = true; }
        public ChunkDeleteStmtNode(LingoNode c) { chunk = c; c.parent = this; }
        @Override public String toLingo(boolean dot) { return "delete " + chunk.toLingo(dot); }
    }

    public static class WhenStmtNode extends LingoNode {
        public final int event;
        public final String script;
        { isStatement = true; }

        public WhenStmtNode(int e, String s) { event = e; script = s; }

        @Override public String toLingo(boolean dot) {
            String eventName = getName(WHEN_EVENT_NAMES, event);
            StringBuilder res = new StringBuilder("when " + eventName + " then ");
            int i = 0;
            while (i < script.length()) {
                while (i < script.length() && Character.isWhitespace(script.charAt(i)) && script.charAt(i) != '\r')
                    i++;
                if (i >= script.length()) break;
                while (i < script.length() && script.charAt(i) != '\r') {
                    res.append(script.charAt(i)); i++;
                }
                if (i >= script.length()) break;
                if (i < script.length() - 1) { res.append("\n  "); }
                i++;
            }
            return res.toString();
        }
    }

    // ==================== Call Nodes ====================

    public static class CallNode extends LingoNode {
        public final String name;
        public final LingoNode argList;

        public CallNode(String n, LingoNode args) {
            this.name = n; this.argList = args; args.parent = this;
            if (args.getValueType() == ValueType.ARG_LIST_NO_RET)
                isStatement = true;
            else
                isExpression = true;
        }

        boolean noParens() {
            if (isStatement) {
                return name.equals("put") || name.equals("return");
            }
            return false;
        }

        @Override public String toLingo(boolean dot) {
            if (isExpression && argList.getArgNodes().isEmpty()) {
                if (name.equals("pi")) return "PI";
                if (name.equals("space")) return "SPACE";
                if (name.equals("void")) return "VOID";
            }
            if (noParens())
                return name + " " + argList.toLingo(dot);
            return name + "(" + argList.toLingo(dot) + ")";
        }
    }

    public static class ObjCallNode extends LingoNode {
        public final String name;
        public final LingoNode argList;

        public ObjCallNode(String n, LingoNode args) {
            this.name = n; this.argList = args; args.parent = this;
            if (args.getValueType() == ValueType.ARG_LIST_NO_RET)
                isStatement = true;
            else
                isExpression = true;
        }

        @Override public String toLingo(boolean dot) {
            var rawArgs = argList.getArgNodes();
            if (rawArgs.isEmpty()) return "???." + name + "()";
            var obj = rawArgs.get(0);
            String left = maybeParenDot(obj, dot);
            StringBuilder res = new StringBuilder(left + "." + name + "(");
            for (int i = 1; i < rawArgs.size(); i++) {
                if (i > 1) res.append(", ");
                res.append(rawArgs.get(i).toLingo(dot));
            }
            res.append(")");
            return res.toString();
        }
    }

    public static class ObjCallV4Node extends LingoNode {
        public final LingoNode obj, argList;

        public ObjCallV4Node(LingoNode o, LingoNode args) {
            this.obj = o; this.argList = args;
            if (args.getValueType() == ValueType.ARG_LIST_NO_RET)
                isStatement = true;
            else
                isExpression = true;
        }

        @Override public String toLingo(boolean dot) {
            return obj.toLingo(dot) + "(" + argList.toLingo(dot) + ")";
        }
    }

    // ==================== Block / Control Flow ====================

    public static class BlockNode extends LingoNode {
        public final List<LingoNode> children = new ArrayList<>();
        public int endPos = -1;
        public CaseNode currentCase;

        { isExpression = true; } // matches ProjectorRays

        public void addChild(LingoNode child) {
            child.parent = this;
            children.add(child);
        }

        @Override public String toLingo(boolean dot) {
            StringBuilder res = new StringBuilder();
            for (LingoNode child : children) {
                res.append(indent(child.toLingo(dot) + "\n"));
            }
            return res.toString();
        }
    }

    public static class HandlerNode extends LingoNode {
        public final String handlerName;
        public final List<String> argumentNames;
        public final List<String> globalNames;
        public final BlockNode block;

        public HandlerNode(String name, List<String> args, List<String> globals) {
            this.handlerName = name;
            this.argumentNames = args;
            this.globalNames = globals;
            this.block = new BlockNode();
            this.block.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            StringBuilder res = new StringBuilder("on " + handlerName);
            if (!argumentNames.isEmpty()) {
                res.append(" ");
                res.append(String.join(", ", argumentNames));
            }
            res.append("\n");
            if (!globalNames.isEmpty()) {
                res.append("  global ");
                res.append(String.join(", ", globalNames));
                res.append("\n");
            }
            res.append(block.toLingo(dot));
            res.append("end");
            return res.toString();
        }
    }

    public static class IfStmtNode extends LingoNode {
        public final LingoNode condition;
        public final BlockNode block1, block2;
        public boolean hasElse;
        { isStatement = true; }

        public IfStmtNode(LingoNode cond) {
            this.condition = cond; cond.parent = this;
            block1 = new BlockNode(); block1.parent = this;
            block2 = new BlockNode(); block2.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            StringBuilder res = new StringBuilder("if " + condition.toLingo(dot) + " then\n");
            res.append(block1.toLingo(dot));
            if (hasElse) {
                res.append("else\n");
                res.append(block2.toLingo(dot));
            }
            res.append("end if");
            return res.toString();
        }
    }

    public static abstract class LoopNodeBase extends LingoNode {
        public final int startIndex;
        LoopNodeBase(int startIndex) {
            this.startIndex = startIndex;
            isStatement = true;
            isLoop = true;
        }
    }

    public static class RepeatWhileStmtNode extends LoopNodeBase {
        public final LingoNode condition;
        public final BlockNode block;

        public RepeatWhileStmtNode(int startIndex, LingoNode cond) {
            super(startIndex);
            this.condition = cond; cond.parent = this;
            block = new BlockNode(); block.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "repeat while " + condition.toLingo(dot) + "\n"
                + block.toLingo(dot) + "end repeat";
        }
    }

    public static class RepeatWithInStmtNode extends LoopNodeBase {
        public final String varName;
        public final LingoNode list;
        public final BlockNode block;

        public RepeatWithInStmtNode(int startIndex, String var, LingoNode lst) {
            super(startIndex);
            this.varName = var; this.list = lst; lst.parent = this;
            block = new BlockNode(); block.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "repeat with " + varName + " in " + list.toLingo(dot) + "\n"
                + block.toLingo(dot) + "end repeat";
        }
    }

    public static class RepeatWithToStmtNode extends LoopNodeBase {
        public final String varName;
        public final LingoNode start, end;
        public final boolean up;
        public final BlockNode block;

        public RepeatWithToStmtNode(int startIndex, String var, LingoNode s, boolean up, LingoNode e) {
            super(startIndex);
            this.varName = var; this.start = s; this.up = up; this.end = e;
            s.parent = this; e.parent = this;
            block = new BlockNode(); block.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            String dir = up ? " to " : " down to ";
            return "repeat with " + varName + " = " + start.toLingo(dot) + dir + end.toLingo(dot)
                + "\n" + block.toLingo(dot) + "end repeat";
        }
    }

    public static class TellStmtNode extends LingoNode {
        public final LingoNode window;
        public final BlockNode block;
        { isStatement = true; }

        public TellStmtNode(LingoNode w) {
            window = w; w.parent = this;
            block = new BlockNode(); block.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            return "tell " + window.toLingo(dot) + "\n"
                + block.toLingo(dot) + "end tell";
        }
    }

    public static class CaseNode extends LingoNode {
        public final LingoNode value;
        public final int expect; // 0=pop, 1=or, 2=next, 3=otherwise
        public static final int EXPECT_POP = 0, EXPECT_OR = 1, EXPECT_NEXT = 2, EXPECT_OTHERWISE = 3;

        public CaseNode nextOr;
        public CaseNode nextCase;
        public BlockNode block;
        public BlockNode otherwise;

        { /* label node */ }

        public CaseNode(LingoNode v, int expect) {
            this.value = v; this.expect = expect; v.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            StringBuilder res = new StringBuilder();
            res.append(value.toLingo(dot));
            if (nextOr != null) {
                res.append(", ").append(nextOr.toLingo(dot));
            } else {
                res.append(":\n");
                if (block != null) res.append(block.toLingo(dot));
            }
            if (nextCase != null) {
                res.append(nextCase.toLingo(dot));
            } else if (otherwise != null) {
                res.append("otherwise:\n");
                res.append(otherwise.toLingo(dot));
            }
            return res.toString();
        }
    }

    public static class CasesStmtNode extends LingoNode {
        public final LingoNode value;
        public CaseNode firstCase;
        public int endPos = -1;
        { isStatement = true; }

        public CasesStmtNode(LingoNode v) {
            value = v; v.parent = this;
        }

        @Override public String toLingo(boolean dot) {
            StringBuilder res = new StringBuilder("case " + value.toLingo(dot) + " of\n");
            if (firstCase != null) {
                res.append(indent(firstCase.toLingo(dot)));
            }
            res.append("end case");
            return res.toString();
        }
    }

    // ==================== Helpers ====================

    /** Wrap node in parens if it's not a "dot-friendly" type */
    static String maybeParenDot(LingoNode node, boolean dot) {
        String s = node.toLingo(dot);
        if (node instanceof VarNode || node instanceof ObjCallNode || node instanceof ObjCallV4Node
            || node instanceof CallNode || node instanceof ObjPropExprNode
            || node instanceof ObjBracketExprNode || node instanceof ObjPropIndexExprNode)
            return s;
        return "(" + s + ")";
    }
}
