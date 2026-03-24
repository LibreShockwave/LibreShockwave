package com.libreshockwave.editor;

import javax.swing.*;
import java.nio.file.Path;

/**
 * Main entry point for the LibreShockwave Editor.
 * A recreation of the Director MX 2004 authoring environment.
 */
public class EditorApp {

    public static void main(String[] args) {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception e) {
            // Fall back to default
        }

        SwingUtilities.invokeLater(() -> {
            EditorFrame frame = new EditorFrame();

            if (args.length > 0) {
                frame.setVisible(true);
                frame.getContext().openFile(Path.of(args[0]));
            } else {
                // Show start screen before making the editor visible
                StartScreenDialog startScreen = new StartScreenDialog(frame);
                Path selected = startScreen.showDialog();

                frame.setVisible(true);

                if (selected != null) {
                    frame.getContext().openFile(selected);
                }
            }
        });
    }
}
