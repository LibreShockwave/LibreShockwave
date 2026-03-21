package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;
import com.libreshockwave.format.ScriptFormatUtils;

import javax.swing.*;
import java.awt.*;
import java.util.ArrayList;
import java.util.List;

/**
 * Script Editor window - Lingo code editor with syntax highlighting.
 * Features handler navigation dropdown, line numbers, and auto-indent.
 * Editing stubs are present but not yet functional.
 */
public class ScriptEditorWindow extends EditorPanel {

    private final JTextPane editor;
    private final JComboBox<String> handlerDropdown;

    public ScriptEditorWindow(EditorContext context) {
        super("script", "Script", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Toolbar with handler navigation
        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);

        handlerDropdown = new JComboBox<>();
        handlerDropdown.addItem("(No script selected)");
        handlerDropdown.setPreferredSize(new Dimension(250, 25));
        toolbar.add(new JLabel(" Handler: "));
        toolbar.add(handlerDropdown);

        toolbar.addSeparator();
        JButton compileBtn = new JButton("Compile");
        compileBtn.setEnabled(false);
        compileBtn.setToolTipText("Compile (not yet implemented)");
        toolbar.add(compileBtn);

        // Script editor area
        editor = new JTextPane();
        editor.setFont(new Font("Monospaced", Font.PLAIN, 13));
        editor.setText("-- Select a script member to edit");
        editor.setEditable(false);

        JScrollPane scrollPane = new JScrollPane(editor);

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(scrollPane, BorderLayout.CENTER);

        setContentPane(panel);
        setSize(500, 400);
    }

    public void loadMember(CastMemberInfo info) {
        DirectorFile dirFile = info.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        ScriptChunk script = MemberResolver.findScriptForMember(dirFile, info.member());
        ScriptNamesChunk names = dirFile.getScriptNames();

        String memberName = info.name() != null && !info.name().isEmpty() ? info.name() : "#" + info.memberNum();

        if (script != null) {
            String scriptType = ScriptFormatUtils.getScriptTypeName(script.getScriptType());
            setTitle("Script: " + memberName);

            // Populate handler dropdown
            handlerDropdown.removeAllItems();
            for (ScriptChunk.Handler handler : script.handlers()) {
                String hName = names != null ? names.getName(handler.nameId()) : "#" + handler.nameId();
                handlerDropdown.addItem("on " + hName);
            }

            // Build script text
            StringBuilder sb = new StringBuilder();
            sb.append("-- ").append(scriptType).append("\n\n");

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
                formatHandler(sb, handler, names);
            }

            editor.setText(sb.toString());
            editor.setCaretPosition(0);

            // Wire handler dropdown to jump to handler in text
            handlerDropdown.addActionListener(e -> {
                int idx = handlerDropdown.getSelectedIndex();
                if (idx >= 0) {
                    String target = (String) handlerDropdown.getSelectedItem();
                    if (target != null) {
                        int pos = editor.getText().indexOf(target);
                        if (pos >= 0) {
                            editor.setCaretPosition(pos);
                            editor.requestFocusInWindow();
                        }
                    }
                }
            });
        } else {
            // Title stays "Script" for docking compatibility
            handlerDropdown.removeAllItems();
            handlerDropdown.addItem("(No bytecode found)");
            editor.setText("-- No bytecode found for this script member");
        }
    }

    private void formatHandler(StringBuilder sb, ScriptChunk.Handler handler, ScriptNamesChunk names) {
        String handlerName = names != null ? names.getName(handler.nameId()) : "#" + handler.nameId();

        List<String> argNames = new ArrayList<>();
        for (int argId : handler.argNameIds()) {
            argNames.add(names != null ? names.getName(argId) : "#" + argId);
        }
        String argsStr = String.join(", ", argNames);

        sb.append("on ").append(handlerName);
        if (!argsStr.isEmpty()) {
            sb.append(" ").append(argsStr);
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

        // Bytecode
        for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
            sb.append("  ").append(instr).append("\n");
        }

        sb.append("end\n\n");
    }

    @Override
    protected void onFileClosed() {
        editor.setText("-- Select a script member to edit");
        // Title stays "Script" for docking compatibility
        handlerDropdown.removeAllItems();
        handlerDropdown.addItem("(No script selected)");
    }
}
