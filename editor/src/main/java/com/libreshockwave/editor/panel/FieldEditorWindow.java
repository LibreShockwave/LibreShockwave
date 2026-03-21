package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.TextChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

import javax.swing.*;
import java.awt.*;

/**
 * Field editor window - simple text field editing.
 * Editing stubs are present but not yet functional.
 */
public class FieldEditorWindow extends EditorPanel {

    private final JTextArea textArea;
    private final JLabel statusLabel;

    public FieldEditorWindow(EditorContext context) {
        super("field", "Field", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);
        toolbar.add(new JLabel(" Field Editor "));
        toolbar.addSeparator();
        toolbar.add(stubButton("Wrap"));
        toolbar.add(stubButton("Scroll"));

        textArea = new JTextArea();
        textArea.setEditable(false);
        textArea.setFont(new Font("SansSerif", Font.PLAIN, 14));
        textArea.setLineWrap(true);
        textArea.setWrapStyleWord(true);
        textArea.setText("No field member selected");

        statusLabel = new JLabel(" Ready");
        statusLabel.setBorder(BorderFactory.createLoweredBevelBorder());

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(new JScrollPane(textArea), BorderLayout.CENTER);
        panel.add(statusLabel, BorderLayout.SOUTH);

        setContentPane(panel);
        setSize(350, 250);
    }

    public void loadMember(CastMemberInfo info) {
        DirectorFile dirFile = info.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        TextChunk textChunk = MemberResolver.findTextForMember(dirFile, info.member());
        if (textChunk != null) {
            String text = textChunk.text().replace("\r\n", "\n").replace("\r", "\n");
            textArea.setText(text);
            textArea.setCaretPosition(0);
            String name = info.name() != null && !info.name().isEmpty() ? info.name() : "#" + info.memberNum();
            setTitle("Field: " + name);
            statusLabel.setText(" " + name + "  " + text.length() + " characters");
        } else {
            textArea.setText("[Field data not found]");
            statusLabel.setText(" No data");
        }
    }

    @Override
    protected void onFileClosed() {
        textArea.setText("No field member selected");
        statusLabel.setText(" Ready");
    }

    private static JButton stubButton(String label) {
        JButton btn = new JButton(label);
        btn.setEnabled(false);
        btn.setToolTipText(label + " (not yet implemented)");
        return btn;
    }
}
