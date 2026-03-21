package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.model.CastMemberInfo;

import javax.swing.*;
import java.awt.*;
import java.awt.image.BufferedImage;

/**
 * Paint window - bitmap editor for cast member images.
 * Editing stubs are present but not yet functional.
 */
public class PaintWindow extends EditorPanel {

    private final JLabel imageLabel;
    private final JLabel statusLabel;

    public PaintWindow(EditorContext context) {
        super("paint", "Paint", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Editing toolbar (stubs - not yet functional)
        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);
        toolbar.add(stubButton("Pencil"));
        toolbar.add(stubButton("Brush"));
        toolbar.add(stubButton("Eraser"));
        toolbar.add(stubButton("Fill"));
        toolbar.add(stubButton("Line"));
        toolbar.add(stubButton("Rect"));
        toolbar.add(stubButton("Oval"));
        toolbar.addSeparator();
        toolbar.add(stubButton("Select"));
        toolbar.add(stubButton("Lasso"));

        // Scrollable image display
        imageLabel = new JLabel("No bitmap selected", SwingConstants.CENTER);
        imageLabel.setHorizontalAlignment(SwingConstants.CENTER);

        JScrollPane scrollPane = new JScrollPane(imageLabel);
        scrollPane.getVerticalScrollBar().setUnitIncrement(16);
        scrollPane.getHorizontalScrollBar().setUnitIncrement(16);
        scrollPane.getViewport().setBackground(Color.DARK_GRAY);

        statusLabel = new JLabel(" Ready");
        statusLabel.setBorder(BorderFactory.createLoweredBevelBorder());

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(scrollPane, BorderLayout.CENTER);
        panel.add(statusLabel, BorderLayout.SOUTH);

        setContentPane(panel);
        setSize(500, 400);
    }

    public void loadMember(CastMemberInfo info) {
        // Use the member's own DirectorFile (essential for external cast bitmaps)
        DirectorFile memberFile = info.member().file();
        if (memberFile == null) memberFile = context.getFile();
        if (memberFile == null) return;

        var bitmapOpt = memberFile.decodeBitmap(info.member());
        if (bitmapOpt.isPresent()) {
            BufferedImage image = bitmapOpt.get().toBufferedImage();
            imageLabel.setIcon(new ImageIcon(image));
            imageLabel.setText(null);
            String name = info.name() != null && !info.name().isEmpty() ? info.name() : "#" + info.memberNum();
            setTitle("Paint: " + name);
            int bitDepth = 0;
            try {
                bitDepth = BitmapInfo.parse(info.member().specificData()).bitDepth();
            } catch (Exception ignored) {}
            statusLabel.setText(String.format(" %s  %dx%d  %d-bit", name, image.getWidth(), image.getHeight(), bitDepth));
        } else {
            imageLabel.setIcon(null);
            imageLabel.setText("Failed to decode bitmap");
            statusLabel.setText(" Error");
        }
    }

    @Override
    protected void onFileClosed() {
        imageLabel.setIcon(null);
        imageLabel.setText("No bitmap selected");
        statusLabel.setText(" Ready");
    }

    private static JButton stubButton(String label) {
        JButton btn = new JButton(label);
        btn.setEnabled(false);
        btn.setToolTipText(label + " (not yet implemented)");
        return btn;
    }
}
