package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.format.InstructionFormatter;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;
import com.libreshockwave.format.ScriptFormatUtils;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.lingo.decompiler.LingoDecompiler;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;

import javax.swing.*;
import java.awt.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Script Editor window - browse scripts per cast library with handler navigation
 * and detailed bytecode display including resolved names and stack descriptions.
 */
public class ScriptEditorWindow extends EditorPanel {

    private final JComboBox<CastEntry> castSelector;
    private final JComboBox<ScriptEntry> scriptSelector;
    private final JComboBox<HandlerEntry> handlerSelector;
    private final JTextArea editor;

    private final JToggleButton lingoToggle;

    // Current state
    private ScriptChunk currentScript;
    private ScriptNamesChunk currentNames;
    private boolean suppressSelectionEvents = false;
    private boolean showStackComments = false;
    private boolean showLingoView = false;

    public ScriptEditorWindow(EditorContext context) {
        super("script", "Script", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Toolbar row 1: cast + script selectors
        JPanel selectorBar = new JPanel();
        selectorBar.setLayout(new BoxLayout(selectorBar, BoxLayout.X_AXIS));

        selectorBar.add(new JLabel(" Cast: "));
        castSelector = new JComboBox<>();
        castSelector.setMaximumSize(new Dimension(200, 25));
        castSelector.addActionListener(e -> { if (!suppressSelectionEvents) onCastSelected(); });
        selectorBar.add(castSelector);

        selectorBar.add(Box.createHorizontalStrut(8));
        selectorBar.add(new JLabel(" Script: "));
        scriptSelector = new JComboBox<>();
        scriptSelector.setMaximumSize(new Dimension(300, 25));
        scriptSelector.addActionListener(e -> { if (!suppressSelectionEvents) onScriptSelected(); });
        selectorBar.add(scriptSelector);

        selectorBar.add(Box.createHorizontalStrut(8));
        selectorBar.add(new JLabel(" Handler: "));
        handlerSelector = new JComboBox<>();
        handlerSelector.setMaximumSize(new Dimension(200, 25));
        handlerSelector.addActionListener(e -> { if (!suppressSelectionEvents) onHandlerSelected(); });
        selectorBar.add(handlerSelector);

        selectorBar.add(Box.createHorizontalStrut(12));

        lingoToggle = new JToggleButton("Lingo");
        lingoToggle.setToolTipText("Toggle between bytecode and decompiled Lingo view");
        lingoToggle.setMaximumSize(new Dimension(80, 25));
        lingoToggle.addActionListener(e -> {
            showLingoView = lingoToggle.isSelected();
            lingoToggle.setText(showLingoView ? "Bytecode" : "Lingo");
            refreshDisplay();
        });
        selectorBar.add(lingoToggle);

        selectorBar.add(Box.createHorizontalGlue());

        // Script display area
        editor = new JTextArea();
        editor.setFont(new Font("Monospaced", Font.PLAIN, 13));
        editor.setText("-- Select a script member to view");
        editor.setEditable(false);
        editor.setTabSize(4);

        JScrollPane scrollPane = new JScrollPane(editor);

        panel.add(selectorBar, BorderLayout.NORTH);
        panel.add(scrollPane, BorderLayout.CENTER);

        setContentPane(panel);
        setSize(700, 500);

        resetAll();
    }

    // --- Called when CastWindow opens a script member via double-click ---

    public void loadMember(CastMemberInfo info) {
        DirectorFile dirFile = info.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        ScriptChunk script = MemberResolver.findScriptForMember(dirFile, info.member());
        if (script == null) {
            editor.setText("-- No bytecode found for this script member");
            return;
        }

        ScriptNamesChunk names = dirFile.getScriptNamesForScript(script);
        if (names == null) names = dirFile.getScriptNames();

        displayScript(script, names);
    }

    // --- EditorPanel lifecycle ---

    @Override
    protected void onFileOpened(DirectorFile file) {
        populateCastSelector();
    }

    @Override
    protected void onCastsLoaded() {
        int prevCast = getSelectedCastNumber();
        populateCastSelector();
        selectCastByNumber(prevCast);
    }

    @Override
    protected void onFileClosed() {
        resetAll();
    }

    // --- Cast selector ---

    private void populateCastSelector() {
        suppressSelectionEvents = true;
        castSelector.removeAllItems();

        Player player = context.getPlayer();
        if (player == null) {
            castSelector.addItem(new CastEntry(0, "Internal"));
            suppressSelectionEvents = false;
            return;
        }

        CastLibManager clm = player.getCastLibManager();
        Map<Integer, CastLib> castLibs = clm.getCastLibs();

        for (int num : new TreeMap<>(castLibs).keySet()) {
            CastLib cl = castLibs.get(num);
            String label = cl.getName();
            if (label == null || label.isEmpty()) label = "Cast " + num;
            if (cl.isExternal() && !cl.isLoaded()) label += " (not loaded)";
            castSelector.addItem(new CastEntry(num, label));
        }

        if (castSelector.getItemCount() == 0) {
            castSelector.addItem(new CastEntry(0, "Internal"));
        }

        suppressSelectionEvents = false;
        onCastSelected();
    }

    private int getSelectedCastNumber() {
        CastEntry e = (CastEntry) castSelector.getSelectedItem();
        return e != null ? e.number : 0;
    }

    private void selectCastByNumber(int num) {
        for (int i = 0; i < castSelector.getItemCount(); i++) {
            if (castSelector.getItemAt(i).number == num) {
                castSelector.setSelectedIndex(i);
                return;
            }
        }
        if (castSelector.getItemCount() > 0) castSelector.setSelectedIndex(0);
    }

    // --- Script selector ---

    private void onCastSelected() {
        suppressSelectionEvents = true;
        scriptSelector.removeAllItems();
        handlerSelector.removeAllItems();
        currentScript = null;
        currentNames = null;

        int castNum = getSelectedCastNumber();
        Player player = context.getPlayer();

        List<ScriptMemberEntry> scriptMembers = new ArrayList<>();

        if (player != null && castNum > 0) {
            CastLib cl = player.getCastLibManager().getCastLib(castNum);
            if (cl != null && cl.isLoaded()) {
                DirectorFile sourceFile = cl.getSourceFile();
                Map<Integer, CastMemberChunk> chunks = cl.getMemberChunks();
                for (var entry : new TreeMap<>(chunks).entrySet()) {
                    CastMemberChunk member = entry.getValue();
                    if (member.memberType() != MemberType.SCRIPT) continue;

                    DirectorFile mFile = member.file() != null ? member.file() : sourceFile;
                    if (mFile == null) continue;

                    ScriptChunk script = MemberResolver.findScriptForMember(mFile, member);
                    if (script == null) continue;

                    ScriptNamesChunk names = mFile.getScriptNamesForScript(script);
                    if (names == null) names = mFile.getScriptNames();

                    String memberName = member.name();
                    if (memberName == null || memberName.isEmpty()) memberName = "Unnamed #" + entry.getKey();
                    String scriptType = ScriptFormatUtils.getScriptTypeName(script.getScriptType());
                    String label = memberName + " (" + scriptType + ")";

                    scriptMembers.add(new ScriptMemberEntry(entry.getKey(), label, script, names));
                }
            }
        } else {
            // Fallback: internal cast via DirectorFile
            DirectorFile file = context.getFile();
            if (file != null) {
                for (CastMemberChunk member : file.getCastMembers()) {
                    if (member.memberType() != MemberType.SCRIPT) continue;

                    ScriptChunk script = MemberResolver.findScriptForMember(file, member);
                    if (script == null) continue;

                    ScriptNamesChunk names = file.getScriptNamesForScript(script);
                    if (names == null) names = file.getScriptNames();

                    String memberName = member.name();
                    if (memberName == null || memberName.isEmpty()) memberName = "Unnamed #" + member.id().value();
                    String scriptType = ScriptFormatUtils.getScriptTypeName(script.getScriptType());
                    String label = memberName + " (" + scriptType + ")";

                    scriptMembers.add(new ScriptMemberEntry(member.id().value(), label, script, names));
                }
            }
        }

        if (scriptMembers.isEmpty()) {
            scriptSelector.addItem(new ScriptEntry(-1, "(No scripts)", null, null));
        } else {
            for (ScriptMemberEntry sm : scriptMembers) {
                scriptSelector.addItem(new ScriptEntry(sm.memberNum, sm.label, sm.script, sm.names));
            }
        }

        suppressSelectionEvents = false;
        onScriptSelected();
    }

    private void onScriptSelected() {
        suppressSelectionEvents = true;
        handlerSelector.removeAllItems();

        ScriptEntry entry = (ScriptEntry) scriptSelector.getSelectedItem();
        if (entry == null || entry.script == null) {
            currentScript = null;
            currentNames = null;
            editor.setText("-- No script selected");
            handlerSelector.addItem(new HandlerEntry(-1, "(No handlers)"));
            suppressSelectionEvents = false;
            return;
        }

        currentScript = entry.script;
        currentNames = entry.names;

        // Populate handler dropdown
        if (currentScript.handlers().isEmpty()) {
            handlerSelector.addItem(new HandlerEntry(-1, "(No handlers)"));
        } else {
            // Add "(All handlers)" option
            handlerSelector.addItem(new HandlerEntry(-1, "(All handlers)"));
            for (int i = 0; i < currentScript.handlers().size(); i++) {
                ScriptChunk.Handler h = currentScript.handlers().get(i);
                String hName = currentNames != null ? currentNames.getName(h.nameId()) : "#" + h.nameId();
                handlerSelector.addItem(new HandlerEntry(i, "on " + hName));
            }
        }

        suppressSelectionEvents = false;
        onHandlerSelected();
    }

    private void onHandlerSelected() {
        if (currentScript == null) return;
        refreshDisplay();
    }

    /** Re-render the display area based on current selection and view mode. */
    private void refreshDisplay() {
        if (currentScript == null) return;

        HandlerEntry entry = (HandlerEntry) handlerSelector.getSelectedItem();
        if (entry == null) return;

        if (showLingoView) {
            displayLingoView(entry);
        } else {
            displayBytecodeView(entry);
        }
    }

    private void displayLingoView(HandlerEntry entry) {
        try {
            LingoDecompiler decompiler = new LingoDecompiler();
            if (entry.handlerIndex < 0) {
                // Show all handlers decompiled
                String result = decompiler.decompile(currentScript, currentNames);
                editor.setText(result);
            } else if (entry.handlerIndex < currentScript.handlers().size()) {
                // Show single handler decompiled
                ScriptChunk.Handler handler = currentScript.handlers().get(entry.handlerIndex);
                String result = decompiler.decompileHandler(handler, currentScript, currentNames);
                editor.setText(result);
            }
        } catch (Exception e) {
            editor.setText("-- Decompilation error: " + e.getMessage()
                + "\n\n-- Falling back to bytecode view:\n\n");
            // Fallback to bytecode
            displayBytecodeView(entry);
            return;
        }
        editor.setCaretPosition(0);
    }

    private void displayBytecodeView(HandlerEntry entry) {
        if (entry.handlerIndex < 0) {
            displayScript(currentScript, currentNames);
        } else if (entry.handlerIndex < currentScript.handlers().size()) {
            ScriptChunk.Handler handler = currentScript.handlers().get(entry.handlerIndex);
            StringBuilder sb = new StringBuilder();
            formatHandler(sb, handler, currentScript, currentNames);
            editor.setText(sb.toString());
            editor.setCaretPosition(0);
        }
    }

    // --- Display ---

    private void displayScript(ScriptChunk script, ScriptNamesChunk names) {
        currentScript = script;
        currentNames = names;

        StringBuilder sb = new StringBuilder();

        String scriptType = ScriptFormatUtils.getScriptTypeName(script.getScriptType());
        sb.append("-- ").append(scriptType);
        String scriptName = script.getScriptName();
        if (scriptName != null && !scriptName.isEmpty()) {
            sb.append(": ").append(scriptName);
        }
        sb.append("\n");
        sb.append("-- Chunk ID: ").append(script.id().value());
        sb.append("  |  Handlers: ").append(script.handlers().size());
        sb.append("  |  Literals: ").append(script.literals().size());
        sb.append("\n\n");

        // Properties
        if (!script.properties().isEmpty()) {
            for (ScriptChunk.PropertyEntry prop : script.properties()) {
                String propName = names != null ? names.getName(prop.nameId()) : "#" + prop.nameId();
                sb.append("property ").append(propName).append("\n");
            }
            sb.append("\n");
        }

        // Globals
        if (!script.globals().isEmpty()) {
            for (ScriptChunk.GlobalEntry global : script.globals()) {
                String globalName = names != null ? names.getName(global.nameId()) : "#" + global.nameId();
                sb.append("global ").append(globalName).append("\n");
            }
            sb.append("\n");
        }

        // Handlers
        for (ScriptChunk.Handler handler : script.handlers()) {
            formatHandler(sb, handler, script, names);
        }

        // Literal pool
        if (!script.literals().isEmpty()) {
            sb.append("-- Literal Pool\n");
            sb.append("-- ").append("-".repeat(60)).append("\n");
            for (int i = 0; i < script.literals().size(); i++) {
                ScriptChunk.LiteralEntry lit = script.literals().get(i);
                String typeStr = ScriptFormatUtils.getLiteralTypeName(lit.type());
                String valStr = ScriptFormatUtils.formatLiteralValue(lit.value(), 80);
                sb.append(String.format("--  [%3d] %-10s %s%n", i, typeStr, valStr));
            }
            sb.append("\n");
        }

        editor.setText(sb.toString());
        editor.setCaretPosition(0);
    }

    private void formatHandler(StringBuilder sb, ScriptChunk.Handler handler,
                               ScriptChunk script, ScriptNamesChunk names) {
        String handlerName = names != null ? names.getName(handler.nameId()) : "#" + handler.nameId();

        List<String> argNames = new ArrayList<>();
        for (int argId : handler.argNameIds()) {
            argNames.add(names != null ? names.getName(argId) : "#" + argId);
        }

        sb.append("on ").append(handlerName);
        if (!argNames.isEmpty()) {
            sb.append(" ").append(String.join(", ", argNames));
        }
        sb.append("\n");

        // Locals
        if (!handler.localNameIds().isEmpty()) {
            List<String> localNames = new ArrayList<>();
            for (int localId : handler.localNameIds()) {
                localNames.add(names != null ? names.getName(localId) : "#" + localId);
            }
            sb.append("  -- locals: ").append(String.join(", ", localNames)).append("\n");
        }

        sb.append("  -- bytecodeOffset: ").append(handler.bytecodeOffset());
        sb.append("  bytecodeLength: ").append(handler.bytecodeLength()).append("\n");
        sb.append("\n");

        // Bytecode with rich annotations
        for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
            // Offset + opcode
            String formatted = InstructionFormatter.format(instr, script, names);
            sb.append("  ").append(formatted);

            // Stack description (toggled by showStackComments)
            if (showStackComments) {
                String stackDesc = getStackDescription(instr, script, names, handler);
                if (stackDesc != null) {
                    int lineLen = formatted.length() + 2;
                    int pad = Math.max(1, 52 - lineLen);
                    sb.append(" ".repeat(pad)).append("; ").append(stackDesc);
                }
            }

            sb.append("\n");
        }

        sb.append("end\n\n");
    }

    /**
     * Returns a human-readable description of what the instruction does to the stack
     * and its overall meaning.
     */
    private String getStackDescription(ScriptChunk.Handler.Instruction instr,
                                       ScriptChunk script, ScriptNamesChunk names,
                                       ScriptChunk.Handler handler) {
        Opcode op = instr.opcode();
        int arg = instr.argument();

        return switch (op) {
            // --- No-arg stack ops ---
            case RET -> "return from handler";
            case RET_FACTORY -> "return from factory/new";
            case PUSH_ZERO -> "push 0";
            case MUL -> "pop b, a; push a * b";
            case ADD -> "pop b, a; push a + b";
            case SUB -> "pop b, a; push a - b";
            case DIV -> "pop b, a; push a / b";
            case MOD -> "pop b, a; push a mod b";
            case INV -> "pop a; push -a";
            case JOIN_STR -> "pop b, a; push a & b";
            case JOIN_PAD_STR -> "pop b, a; push a && b";
            case LT -> "pop b, a; push a < b";
            case LT_EQ -> "pop b, a; push a <= b";
            case NT_EQ -> "pop b, a; push a <> b";
            case EQ -> "pop b, a; push a = b";
            case GT -> "pop b, a; push a > b";
            case GT_EQ -> "pop b, a; push a >= b";
            case AND -> "pop b, a; push a AND b";
            case OR -> "pop b, a; push a OR b";
            case NOT -> "pop a; push NOT a";
            case CONTAINS_STR -> "pop b, a; push a contains b";
            case CONTAINS_0_STR -> "pop b, a; push b starts a";
            case GET_CHUNK -> "pop chunkExpr; push chunk value";
            case HILITE_CHUNK -> "hilite chunk expression";
            case ONTO_SPR -> "spriteBox: sprite intersects sprite";
            case INTO_SPR -> "spriteBox: sprite within sprite";
            case GET_FIELD -> "pop fieldRef; push field value";
            case START_TELL -> "begin tell target";
            case END_TELL -> "end tell";
            case PUSH_LIST -> "pop N items; push [list]";
            case PUSH_PROP_LIST -> "pop N key/value pairs; push [propList]";
            case SWAP -> "swap top two stack values";

            // --- Arg ops: push ---
            case PUSH_INT8 -> "push " + arg;
            case PUSH_INT16 -> "push " + arg;
            case PUSH_INT32 -> "push " + arg;
            case PUSH_FLOAT32 -> "push " + Float.intBitsToFloat(arg);
            case PUSH_CONS -> {
                if (arg >= 0 && arg < script.literals().size()) {
                    var lit = script.literals().get(arg);
                    yield "push literal[" + arg + "] = " + ScriptFormatUtils.formatLiteralValue(lit.value(), 40);
                }
                yield "push literal[" + arg + "]";
            }
            case PUSH_SYMB -> {
                String sym = resolveName(names, arg);
                yield "push #" + sym;
            }
            case PUSH_VAR_REF -> {
                String vrName = resolveName(names, arg);
                yield "push @" + vrName + " (variable ref)";
            }
            case PUSH_CHUNK_VAR_REF -> {
                String crName = resolveName(names, arg);
                yield "push chunk varRef @" + crName;
            }
            case PUSH_ARG_LIST -> "build argList, count=" + arg;
            case PUSH_ARG_LIST_NO_RET -> "build argList (no return), count=" + arg;

            // --- Variable get/set ---
            case GET_GLOBAL, GET_GLOBAL2 -> {
                String gName = resolveName(names, arg);
                yield "push global(" + gName + ")";
            }
            case SET_GLOBAL, SET_GLOBAL2 -> {
                String gName = resolveName(names, arg);
                yield "pop -> global(" + gName + ")";
            }
            case GET_PROP -> {
                String pName = resolveName(names, arg);
                yield "push property(" + pName + ")";
            }
            case SET_PROP -> {
                String pName = resolveName(names, arg);
                yield "pop -> property(" + pName + ")";
            }
            case GET_PARAM -> {
                String paramName = resolveParam(names, handler, arg);
                yield "push param(" + paramName + ")";
            }
            case SET_PARAM -> {
                String paramName = resolveParam(names, handler, arg);
                yield "pop -> param(" + paramName + ")";
            }
            case GET_LOCAL -> {
                String lName = resolveLocal(names, handler, arg);
                yield "push local(" + lName + ")";
            }
            case SET_LOCAL -> {
                String lName = resolveLocal(names, handler, arg);
                yield "pop -> local(" + lName + ")";
            }
            case GET_OBJ_PROP -> {
                String opName = resolveName(names, arg);
                yield "pop obj; push obj." + opName;
            }
            case SET_OBJ_PROP -> {
                String opName = resolveName(names, arg);
                yield "pop val, obj; obj." + opName + " = val";
            }
            case GET_MOVIE_PROP -> {
                String mpName = resolveName(names, arg);
                yield "push the " + mpName;
            }
            case SET_MOVIE_PROP -> {
                String mpName = resolveName(names, arg);
                yield "pop -> the " + mpName;
            }
            case GET_TOP_LEVEL_PROP -> {
                String tlName = resolveName(names, arg);
                yield "push top-level " + tlName;
            }
            case GET_CHAINED_PROP -> {
                String cpName = resolveName(names, arg);
                yield "pop obj; push obj." + cpName + " (chained)";
            }

            // --- Calls ---
            case LOCAL_CALL -> {
                String cName = resolveName(names, arg);
                yield "call " + cName + "(args)";
            }
            case EXT_CALL -> {
                String cName = resolveName(names, arg);
                yield "call external " + cName + "(args)";
            }
            case OBJ_CALL -> {
                String cName = resolveName(names, arg);
                yield "pop obj; call obj." + cName + "(args)";
            }
            case OBJ_CALL_V4 -> {
                String cName = resolveName(names, arg);
                yield "call (v4) " + cName + "(args)";
            }
            case TELL_CALL -> {
                String cName = resolveName(names, arg);
                yield "tell target: call " + cName + "(args)";
            }

            // --- Control flow ---
            case JMP -> "jump -> offset " + arg;
            case JMP_IF_Z -> "pop; jump if FALSE -> offset " + arg;
            case END_REPEAT -> "jump (end repeat) -> offset " + arg;

            // --- Misc ---
            case PUT -> {
                String putName = resolveName(names, arg);
                yield "put value into " + putName;
            }
            case PUT_CHUNK -> {
                String pcName = resolveName(names, arg);
                yield "put value into chunk of " + pcName;
            }
            case DELETE_CHUNK -> {
                String dcName = resolveName(names, arg);
                yield "delete chunk of " + dcName;
            }
            case GET -> {
                String getName = resolveName(names, arg);
                yield "push " + getName;
            }
            case SET -> {
                String setName = resolveName(names, arg);
                yield "pop -> " + setName;
            }
            case THE_BUILTIN -> {
                String bName = resolveName(names, arg);
                yield "push the " + bName + " (builtin)";
            }
            case NEW_OBJ -> {
                String nName = resolveName(names, arg);
                yield "pop args; push new(" + nName + ")";
            }
            case PEEK -> "peek stack[" + arg + "] (dup)";
            case POP -> "discard top " + arg + " stack value(s)";
            case CALL_JAVASCRIPT -> "call JavaScript bridge";
            default -> null;
        };
    }

    private static String resolveName(ScriptNamesChunk names, int nameId) {
        if (names != null && nameId >= 0 && nameId < names.names().size()) {
            return names.getName(nameId);
        }
        return "#" + nameId;
    }

    private static String resolveParam(ScriptNamesChunk names, ScriptChunk.Handler handler, int index) {
        if (index >= 0 && index < handler.argNameIds().size()) {
            int nameId = handler.argNameIds().get(index);
            return resolveName(names, nameId);
        }
        return "param" + index;
    }

    private static String resolveLocal(ScriptNamesChunk names, ScriptChunk.Handler handler, int index) {
        if (index >= 0 && index < handler.localNameIds().size()) {
            int nameId = handler.localNameIds().get(index);
            return resolveName(names, nameId);
        }
        return "local" + index;
    }

    // --- Reset ---

    private void resetAll() {
        suppressSelectionEvents = true;
        castSelector.removeAllItems();
        castSelector.addItem(new CastEntry(0, "Internal"));
        scriptSelector.removeAllItems();
        scriptSelector.addItem(new ScriptEntry(-1, "(No scripts)", null, null));
        handlerSelector.removeAllItems();
        handlerSelector.addItem(new HandlerEntry(-1, "(No handlers)"));
        currentScript = null;
        currentNames = null;
        editor.setText("-- Select a script member to view");
        suppressSelectionEvents = false;
    }

    // --- Entry records for combo boxes ---

    private record CastEntry(int number, String label) {
        @Override public String toString() { return label; }
    }

    private record ScriptEntry(int memberNum, String label, ScriptChunk script, ScriptNamesChunk names) {
        @Override public String toString() { return label; }
    }

    private record HandlerEntry(int handlerIndex, String label) {
        @Override public String toString() { return label; }
    }

    private record ScriptMemberEntry(int memberNum, String label, ScriptChunk script, ScriptNamesChunk names) {}
}
