package com.libreshockwave.editor;

import com.libreshockwave.editor.panel.DetailedStackWindow;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.debug.DebugController;

import javax.swing.*;
import java.awt.event.InputEvent;
import java.awt.event.KeyEvent;
import java.util.Map;

/**
 * Director MX 2004 menu bar recreation.
 * Provides File, Edit, View, Insert, Modify, Control, Debug, Window, and Help menus.
 */
public class EditorMenuBar extends JMenuBar {

    private final EditorFrame editorFrame;
    private final EditorContext context;
    private final DetailedStackWindow detailedStackWindow;

    public EditorMenuBar(EditorFrame editorFrame, EditorContext context) {
        this.editorFrame = editorFrame;
        this.context = context;
        this.detailedStackWindow = new DetailedStackWindow();
        buildMenus();
    }

    public DetailedStackWindow getDetailedStackWindow() {
        return detailedStackWindow;
    }

    private void buildMenus() {
        add(buildFileMenu());
        add(buildEditMenu());
        add(buildViewMenu());
        add(buildInsertMenu());
        add(buildModifyMenu());
        add(buildControlMenu());
        add(buildDebugMenu());
        add(buildWindowMenu());
        add(buildHelpMenu());
    }

    // ---- File Menu ----

    private JMenu buildFileMenu() {
        JMenu menu = new JMenu("File");
        menu.setMnemonic(KeyEvent.VK_F);

        JMenuItem newMovie = new JMenuItem("New Movie");
        newMovie.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_N, InputEvent.CTRL_DOWN_MASK));
        newMovie.setEnabled(false);
        menu.add(newMovie);

        JMenuItem newCast = new JMenuItem("New Cast");
        newCast.setEnabled(false);
        menu.add(newCast);

        menu.addSeparator();

        JMenuItem open = new JMenuItem("Open...");
        open.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_O, InputEvent.CTRL_DOWN_MASK));
        open.addActionListener(e -> editorFrame.openFileDialog());
        menu.add(open);

        JMenuItem close = new JMenuItem("Close");
        close.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_W, InputEvent.CTRL_DOWN_MASK));
        close.addActionListener(e -> context.closeFile());
        menu.add(close);

        menu.addSeparator();

        JMenuItem save = new JMenuItem("Save");
        save.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_S, InputEvent.CTRL_DOWN_MASK));
        save.setEnabled(false);
        menu.add(save);

        JMenuItem saveAs = new JMenuItem("Save As...");
        saveAs.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_S, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        saveAs.setEnabled(false);
        menu.add(saveAs);

        JMenuItem saveAll = new JMenuItem("Save All");
        saveAll.setEnabled(false);
        menu.add(saveAll);

        menu.addSeparator();

        JMenuItem importItem = new JMenuItem("Import...");
        importItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_R, InputEvent.CTRL_DOWN_MASK));
        importItem.setEnabled(false);
        menu.add(importItem);

        JMenuItem exportItem = new JMenuItem("Export...");
        exportItem.setEnabled(false);
        menu.add(exportItem);

        menu.addSeparator();

        // Preferences submenu
        JMenu prefsMenu = new JMenu("Preferences");
        addStubItem(prefsMenu, "General...");
        addStubItem(prefsMenu, "Network...");
        addStubItem(prefsMenu, "Script...");
        addStubItem(prefsMenu, "Sprite...");
        addStubItem(prefsMenu, "Paint...");
        menu.add(prefsMenu);

        menu.addSeparator();

        JMenuItem exit = new JMenuItem("Exit");
        exit.addActionListener(e -> System.exit(0));
        menu.add(exit);

        return menu;
    }

    // ---- Edit Menu ----

    private JMenu buildEditMenu() {
        JMenu menu = new JMenu("Edit");
        menu.setMnemonic(KeyEvent.VK_E);

        JMenuItem undo = addStubItem(menu, "Undo");
        undo.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_Z, InputEvent.CTRL_DOWN_MASK));

        JMenuItem redo = addStubItem(menu, "Redo");
        redo.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_Y, InputEvent.CTRL_DOWN_MASK));

        menu.addSeparator();

        JMenuItem cut = addStubItem(menu, "Cut");
        cut.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_X, InputEvent.CTRL_DOWN_MASK));

        JMenuItem copy = addStubItem(menu, "Copy");
        copy.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_C, InputEvent.CTRL_DOWN_MASK));

        JMenuItem paste = addStubItem(menu, "Paste");
        paste.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_V, InputEvent.CTRL_DOWN_MASK));

        JMenuItem clear = addStubItem(menu, "Clear");
        clear.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_DELETE, 0));

        JMenuItem selectAll = addStubItem(menu, "Select All");
        selectAll.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_A, InputEvent.CTRL_DOWN_MASK));

        menu.addSeparator();

        // Find submenu
        JMenu findMenu = new JMenu("Find");
        JMenuItem find = addStubItem(findMenu, "Find...");
        find.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F, InputEvent.CTRL_DOWN_MASK));
        JMenuItem findAgain = addStubItem(findMenu, "Find Again");
        findAgain.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_G, InputEvent.CTRL_DOWN_MASK));
        JMenuItem replace = addStubItem(findMenu, "Replace...");
        replace.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_H, InputEvent.CTRL_DOWN_MASK));
        addStubItem(findMenu, "Find Selection");
        menu.add(findMenu);

        menu.addSeparator();

        addStubItem(menu, "Edit Sprite Frames");
        addStubItem(menu, "Edit Entire Sprite");

        menu.addSeparator();

        addStubItem(menu, "Exchange Cast Members");

        return menu;
    }

    // ---- View Menu ----

    private JMenu buildViewMenu() {
        JMenu menu = new JMenu("View");
        menu.setMnemonic(KeyEvent.VK_V);

        // Zoom submenu
        JMenu zoomMenu = new JMenu("Zoom");
        for (String zoom : new String[]{"25%", "50%", "100%", "200%", "400%"}) {
            addStubItem(zoomMenu, zoom);
        }
        menu.add(zoomMenu);

        menu.addSeparator();

        // Sprite Overlay submenu
        JMenu overlayMenu = new JMenu("Sprite Overlay");
        overlayMenu.add(new JCheckBoxMenuItem("Show Info"));
        overlayMenu.add(new JCheckBoxMenuItem("Show Paths"));
        menu.add(overlayMenu);

        menu.addSeparator();

        menu.add(new JCheckBoxMenuItem("Sprite Toolbar"));
        menu.add(new JCheckBoxMenuItem("Keyframes"));

        menu.addSeparator();

        // Grids submenu
        JMenu gridsMenu = new JMenu("Grids");
        gridsMenu.add(new JCheckBoxMenuItem("Show"));
        gridsMenu.add(new JCheckBoxMenuItem("Snap To"));
        addStubItem(gridsMenu, "Settings...");
        menu.add(gridsMenu);

        // Guides submenu
        JMenu guidesMenu = new JMenu("Guides");
        guidesMenu.add(new JCheckBoxMenuItem("Show"));
        guidesMenu.add(new JCheckBoxMenuItem("Snap To"));
        menu.add(guidesMenu);

        return menu;
    }

    // ---- Insert Menu ----

    private JMenu buildInsertMenu() {
        JMenu menu = new JMenu("Insert");
        menu.setMnemonic(KeyEvent.VK_I);

        JMenuItem keyframe = addStubItem(menu, "Keyframe");
        keyframe.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_K, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK));

        menu.addSeparator();

        addStubItem(menu, "Marker");

        menu.addSeparator();

        addStubItem(menu, "Remove Frame");

        menu.addSeparator();

        // Media Element submenu
        JMenu mediaMenu = new JMenu("Media Element");
        addStubItem(mediaMenu, "Bitmap");
        addStubItem(mediaMenu, "Text");
        addStubItem(mediaMenu, "Script");
        addStubItem(mediaMenu, "Shape");
        addStubItem(mediaMenu, "Film Loop");
        addStubItem(mediaMenu, "Sound");
        menu.add(mediaMenu);

        return menu;
    }

    // ---- Modify Menu ----

    private JMenu buildModifyMenu() {
        JMenu menu = new JMenu("Modify");
        menu.setMnemonic(KeyEvent.VK_M);

        // Movie submenu
        JMenu movieMenu = new JMenu("Movie");
        addStubItem(movieMenu, "Properties...");
        addStubItem(movieMenu, "Casts...");

        JMenuItem extParams = new JMenuItem("External Parameters...");
        extParams.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_E, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        extParams.addActionListener(e -> {
            JFrame parentFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
            Map<String, String> current = new java.util.LinkedHashMap<>(context.getExternalParams());
            ExternalParamsDialog dialog = new ExternalParamsDialog(parentFrame, current);
            Map<String, String> result = dialog.showDialog();
            if (result != null) {
                context.setExternalParams(result);
            }
        });
        movieMenu.add(extParams);

        menu.add(movieMenu);

        menu.addSeparator();

        // Sprite submenu
        JMenu spriteMenu = new JMenu("Sprite");
        addStubItem(spriteMenu, "Properties...");
        addStubItem(spriteMenu, "Tweening...");
        menu.add(spriteMenu);

        menu.addSeparator();

        // Cast Member submenu
        JMenu castMenu = new JMenu("Cast Member");
        addStubItem(castMenu, "Properties...");
        menu.add(castMenu);

        menu.addSeparator();

        // Frame submenu
        JMenu frameMenu = new JMenu("Frame");
        addStubItem(frameMenu, "Tempo...");
        addStubItem(frameMenu, "Palette...");
        addStubItem(frameMenu, "Transition...");
        addStubItem(frameMenu, "Sound...");
        menu.add(frameMenu);

        menu.addSeparator();

        addStubItem(menu, "Font...");
        addStubItem(menu, "Paragraph...");

        return menu;
    }

    // ---- Control Menu ----

    private JMenu buildControlMenu() {
        JMenu menu = new JMenu("Control");
        menu.setMnemonic(KeyEvent.VK_C);

        JMenuItem play = new JMenuItem("Play");
        play.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_P, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK));
        play.addActionListener(e -> context.play());
        menu.add(play);

        JMenuItem stop = new JMenuItem("Stop");
        stop.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_PERIOD, InputEvent.CTRL_DOWN_MASK));
        stop.addActionListener(e -> context.stop());
        menu.add(stop);

        JMenuItem rewind = new JMenuItem("Rewind");
        rewind.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_R, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK));
        rewind.addActionListener(e -> context.rewind());
        menu.add(rewind);

        menu.addSeparator();

        JMenuItem stepFwd = new JMenuItem("Step Forward");
        stepFwd.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_RIGHT, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK));
        stepFwd.addActionListener(e -> context.stepForward());
        menu.add(stepFwd);

        JMenuItem stepBack = new JMenuItem("Step Backward");
        stepBack.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_LEFT, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK));
        stepBack.addActionListener(e -> context.stepBackward());
        menu.add(stepBack);

        menu.addSeparator();

        JCheckBoxMenuItem loop = new JCheckBoxMenuItem("Loop Playback");
        loop.setSelected(true);
        menu.add(loop);

        return menu;
    }

    // ---- Debug Menu ----

    private JMenu buildDebugMenu() {
        JMenu menu = new JMenu("Debug");
        menu.setMnemonic(KeyEvent.VK_D);

        JMenuItem stepInto = new JMenuItem("Step Into");
        stepInto.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F11, 0));
        stepInto.addActionListener(e -> {
            DebugController dc = context.getDebugController();
            if (dc != null) dc.stepInto();
        });
        menu.add(stepInto);

        JMenuItem stepOver = new JMenuItem("Step Over");
        stepOver.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F10, 0));
        stepOver.addActionListener(e -> {
            DebugController dc = context.getDebugController();
            if (dc != null) dc.stepOver();
        });
        menu.add(stepOver);

        JMenuItem stepOut = new JMenuItem("Step Out");
        stepOut.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F11, InputEvent.SHIFT_DOWN_MASK));
        stepOut.addActionListener(e -> {
            DebugController dc = context.getDebugController();
            if (dc != null) dc.stepOut();
        });
        menu.add(stepOut);

        JMenuItem continueItem = new JMenuItem("Continue");
        continueItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F5, 0));
        continueItem.addActionListener(e -> {
            DebugController dc = context.getDebugController();
            if (dc != null) dc.continueExecution();
        });
        menu.add(continueItem);

        menu.addSeparator();

        JMenuItem toggleBp = new JMenuItem("Toggle Breakpoint");
        toggleBp.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_F9, 0));
        toggleBp.setEnabled(false); // Breakpoints are toggled in the bytecode panel
        menu.add(toggleBp);

        JMenuItem clearBp = new JMenuItem("Clear All Breakpoints");
        clearBp.addActionListener(e -> {
            DebugController dc = context.getDebugController();
            if (dc != null) {
                dc.clearAllBreakpoints();
                context.clearSavedBreakpoints();
            }
        });
        menu.add(clearBp);

        menu.addSeparator();

        JCheckBoxMenuItem detailedStackItem = new JCheckBoxMenuItem("Detailed Stack Window", false);
        detailedStackItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_S, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        detailedStackItem.addActionListener(e -> detailedStackWindow.setVisible(detailedStackItem.isSelected()));
        menu.add(detailedStackItem);

        JMenuItem traceItem = new JMenuItem("Trace Handler...");
        traceItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_T, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        traceItem.addActionListener(e -> showTraceHandlerDialog());
        menu.add(traceItem);

        return menu;
    }

    private void showTraceHandlerDialog() {
        Player player = context.getPlayer();
        if (player == null) {
            JOptionPane.showMessageDialog(editorFrame, "No movie loaded.", "Trace Handler", JOptionPane.WARNING_MESSAGE);
            return;
        }
        var vm = player.getVM();
        var current = vm.getTracedHandlers();
        String currentStr = current.isEmpty() ? "" : String.join(", ", current);
        String input = JOptionPane.showInputDialog(editorFrame,
            "Enter handler names to trace (comma-separated), or clear to remove all:\n" +
            "Current: " + (currentStr.isEmpty() ? "(none)" : currentStr),
            "Trace Handler", JOptionPane.PLAIN_MESSAGE);
        if (input == null) return;
        vm.clearTraceHandlers();
        if (!input.isBlank()) {
            for (String name : input.split(",")) {
                String trimmed = name.trim();
                if (!trimmed.isEmpty()) {
                    vm.addTraceHandler(trimmed);
                }
            }
        }
    }

    // ---- Window Menu ----

    private JMenu buildWindowMenu() {
        JMenu menu = new JMenu("Window");
        menu.setMnemonic(KeyEvent.VK_W);

        // Core panels
        addWindowToggle(menu, "stage", "Stage", KeyEvent.VK_1, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "score", "Score", KeyEvent.VK_2, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "cast", "Cast", KeyEvent.VK_3, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "property-inspector", "Property Inspector", KeyEvent.VK_4, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "script", "Script", KeyEvent.VK_0, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "message", "Message", KeyEvent.VK_M, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK);
        addWindowToggle(menu, "tool-palette", "Tool Palette", KeyEvent.VK_7, InputEvent.CTRL_DOWN_MASK);

        menu.addSeparator();

        // Media panels
        addWindowToggle(menu, "paint", "Paint", KeyEvent.VK_5, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "vector-shape", "Vector Shape", 0, 0);
        addWindowToggle(menu, "text", "Text", KeyEvent.VK_6, InputEvent.CTRL_DOWN_MASK);
        addWindowToggle(menu, "field", "Field", 0, 0);
        addWindowToggle(menu, "sound", "Sound", 0, 0);
        addWindowToggle(menu, "color-palettes", "Color Palettes", KeyEvent.VK_7, InputEvent.CTRL_DOWN_MASK | InputEvent.ALT_DOWN_MASK);

        menu.addSeparator();

        // Advanced panels
        addWindowToggle(menu, "bytecode-debugger", "Bytecode Debugger", KeyEvent.VK_D, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK);

        menu.addSeparator();

        JMenuItem resetLayout = new JMenuItem("Reset Layout");
        resetLayout.addActionListener(e -> editorFrame.resetLayout());
        menu.add(resetLayout);

        return menu;
    }

    private void addWindowToggle(JMenu menu, String panelId, String label, int keyCode, int modifiers) {
        JCheckBoxMenuItem item = new JCheckBoxMenuItem(label, true);
        if (keyCode != 0) {
            item.setAccelerator(KeyStroke.getKeyStroke(keyCode, modifiers));
        }
        item.addActionListener(e -> editorFrame.togglePanel(panelId, item.isSelected()));

        // Update checkbox state when the Window menu is about to be shown
        menu.addMenuListener(new javax.swing.event.MenuListener() {
            @Override
            public void menuSelected(javax.swing.event.MenuEvent e) {
                item.setSelected(editorFrame.isPanelVisible(panelId));
            }
            @Override public void menuDeselected(javax.swing.event.MenuEvent e) {}
            @Override public void menuCanceled(javax.swing.event.MenuEvent e) {}
        });

        menu.add(item);
    }

    // ---- Help Menu ----

    private JMenu buildHelpMenu() {
        JMenu menu = new JMenu("Help");
        menu.setMnemonic(KeyEvent.VK_H);

        JMenuItem about = new JMenuItem("About LibreShockwave Editor");
        about.addActionListener(e -> JOptionPane.showMessageDialog(editorFrame,
            "LibreShockwave Editor\n\n" +
            "A recreation of Macromedia Director MX 2004.\n\n" +
            "Part of the LibreShockwave project.",
            "About",
            JOptionPane.INFORMATION_MESSAGE));
        menu.add(about);

        return menu;
    }

    // Helper

    private JMenuItem addStubItem(JMenu menu, String text) {
        JMenuItem item = new JMenuItem(text);
        item.setEnabled(false);
        menu.add(item);
        return item;
    }
}
