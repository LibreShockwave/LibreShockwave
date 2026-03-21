package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.TextChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

import javax.swing.*;
import java.awt.*;

/**
 * Text editor window - rich text editing for text cast members.
 * Editing stubs are present but not yet functional.
 */
public class TextEditorWindow extends EditorPanel {

    private final JTextPane textPane;
    private final JLabel statusLabel;

    public TextEditorWindow(EditorContext context) {
        super("text", "Text", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Formatting toolbar (stubs - not yet functional)
        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);
        toolbar.add(stubButton("B"));
        toolbar.add(stubButton("I"));
        toolbar.add(stubButton("U"));
        toolbar.addSeparator();
        JComboBox<String> fontBox = new JComboBox<>(new String[]{"Arial", "Times New Roman", "Courier New"});
        fontBox.setEnabled(false);
        toolbar.add(fontBox);
        JComboBox<String> sizeBox = new JComboBox<>(new String[]{"12", "14", "16", "18", "24", "36"});
        sizeBox.setEnabled(false);
        toolbar.add(sizeBox);

        textPane = new JTextPane();
        textPane.setEditable(false);
        textPane.setFont(new Font("SansSerif", Font.PLAIN, 14));
        textPane.setText("No text member selected");

        statusLabel = new JLabel(" Ready");
        statusLabel.setBorder(BorderFactory.createLoweredBevelBorder());

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(new JScrollPane(textPane), BorderLayout.CENTER);
        panel.add(statusLabel, BorderLayout.SOUTH);

        setContentPane(panel);
        setSize(400, 300);
    }

    public void loadMember(CastMemberInfo info) {
        DirectorFile dirFile = info.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        TextChunk textChunk = MemberResolver.findTextForMember(dirFile, info.member());
        if (textChunk != null) {
            String text = textChunk.text().replace("\r\n", "\n").replace("\r", "\n");
            textPane.setText(text);
            textPane.setCaretPosition(0);
            String name = info.name() != null && !info.name().isEmpty() ? info.name() : "#" + info.memberNum();
            setTitle("Text: " + name);
            statusLabel.setText(" " + name + "  " + textChunk.runs().size() + " formatting run(s)");
        } else {
            textPane.setText("[Text data not found]");
            statusLabel.setText(" No data");
        }
    }

    @Override
    protected void onFileClosed() {
        textPane.setText("No text member selected");
        statusLabel.setText(" Ready");
    }

    private static JButton stubButton(String label) {
        JButton btn = new JButton(label);
        btn.setEnabled(false);
        btn.setToolTipText(label + " (not yet implemented)");
        return btn;
    }
}
