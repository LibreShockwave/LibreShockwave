package com.libreshockwave.editor.debug.ui;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.lingo.decompiler.LingoDecompiler;
import com.libreshockwave.player.debug.Breakpoint;
import com.libreshockwave.player.debug.DebugController;
import com.libreshockwave.vm.trace.InstructionAnnotator;

import javax.swing.*;
import javax.swing.border.TitledBorder;
import java.awt.*;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.event.MouseMotionAdapter;
import java.util.ArrayList;
import java.util.List;

/**
 * Panel containing the bytecode/Lingo JList with mouse handling and highlighting.
 * Supports toggling between bytecode and decompiled Lingo view.
 */
public class BytecodeListPanel extends JPanel {

    /**
     * Listener for bytecode list events.
     */
    public interface BytecodeListListener {
        void onBreakpointToggleRequested(int offset);
        void onNavigateToHandler(String handlerName);
        void onShowHandlerDetails(String handlerName);
    }

    private final DefaultListModel<InstructionDisplayItem> bytecodeModel;
    private final JList<InstructionDisplayItem> bytecodeList;
    private final BytecodeContextMenu contextMenu;
    private final List<InstructionDisplayItem> currentInstructions = new ArrayList<>();
    private final JToggleButton lingoToggle;
    private final TitledBorder titledBorder;

    private DebugController controller;
    private HandlerNavigator navigator;
    private int currentScriptId = -1;
    private String currentHandlerName = null;
    private int currentInstructionIndex = -1;
    private BytecodeListListener listener;

    // Lingo view state
    private boolean showLingoView = false;
    private ScriptChunk currentScript;
    private ScriptChunk.Handler currentHandler;

    public BytecodeListPanel() {
        setLayout(new BorderLayout());
        titledBorder = new TitledBorder("Bytecode");
        setBorder(titledBorder);

        bytecodeModel = new DefaultListModel<>();
        bytecodeList = new JList<>(bytecodeModel);
        bytecodeList.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 11));
        bytecodeList.setCellRenderer(new BytecodeCellRenderer());
        bytecodeList.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);

        initMouseListeners();

        contextMenu = new BytecodeContextMenu(bytecodeList, bytecodeModel);

        JScrollPane bytecodeScroll = new JScrollPane(bytecodeList);
        bytecodeScroll.setPreferredSize(new Dimension(480, 200));
        add(bytecodeScroll, BorderLayout.CENTER);

        // Bottom bar: legend + toggle
        JPanel bottomBar = new JPanel(new BorderLayout());
        JLabel legend = new JLabel("<html>" +
            "<font color='red'>\u25CF</font>=breakpoint &nbsp; " +
            "<font color='gray'>\u25CB</font>=disabled &nbsp; " +
            "<font color='#DAA520'>\u25B6</font>=current &nbsp; " +
            "<font color='blue'><u>blue</u></font>=navigate</html>");
        legend.setFont(new Font(Font.SANS_SERIF, Font.PLAIN, 10));
        bottomBar.add(legend, BorderLayout.CENTER);

        lingoToggle = new JToggleButton("Lingo");
        lingoToggle.setToolTipText("Toggle between bytecode and decompiled Lingo view");
        lingoToggle.setFont(new Font(Font.SANS_SERIF, Font.PLAIN, 10));
        lingoToggle.setMargin(new Insets(1, 6, 1, 6));
        lingoToggle.addActionListener(e -> {
            showLingoView = lingoToggle.isSelected();
            lingoToggle.setText(showLingoView ? "Bytecode" : "Lingo");
            titledBorder.setTitle(showLingoView ? "Lingo" : "Bytecode");
            repaint();
            reloadCurrentView();
        });
        bottomBar.add(lingoToggle, BorderLayout.EAST);

        add(bottomBar, BorderLayout.SOUTH);
    }

    private void initMouseListeners() {
        // Click to toggle breakpoints or navigate to call targets
        bytecodeList.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
                int index = bytecodeList.locationToIndex(e.getPoint());
                if (index < 0 || index >= bytecodeModel.size()) {
                    return;
                }

                InstructionDisplayItem item = bytecodeModel.get(index);

                // Double-click or click on left margin (gutter) -> toggle breakpoint
                if (e.getClickCount() == 2 || e.getX() < 20) {
                    if (item.getOffset() < 0) return; // structural line, no breakpoint
                    if (controller != null && currentScriptId >= 0 && currentHandlerName != null) {
                        controller.toggleBreakpoint(currentScriptId, currentHandlerName, item.getOffset());
                        refreshBreakpointMarkers();
                    }
                    return;
                }

                // Single click on navigable call instruction -> navigate to handler definition
                if (e.getClickCount() == 1 && item.isNavigableCall()) {
                    String targetName = item.getCallTargetName();
                    if (targetName != null && listener != null) {
                        listener.onNavigateToHandler(targetName);
                    }
                }
            }
        });

        // Change cursor to hand when hovering over navigable call instructions
        bytecodeList.addMouseMotionListener(new MouseMotionAdapter() {
            @Override
            public void mouseMoved(MouseEvent e) {
                int index = bytecodeList.locationToIndex(e.getPoint());
                if (index >= 0 && index < bytecodeModel.size()) {
                    InstructionDisplayItem item = bytecodeModel.get(index);
                    if (item.isNavigableCall() && e.getX() >= 20) {
                        bytecodeList.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
                        return;
                    }
                }
                bytecodeList.setCursor(Cursor.getDefaultCursor());
            }
        });
    }

    /**
     * Reload current handler in the active view mode (bytecode or Lingo).
     */
    private void reloadCurrentView() {
        if (currentScript != null && currentHandler != null) {
            loadHandlerBytecode(currentScript, currentHandler);
            // Re-highlight current instruction if we had one
            if (currentInstructionIndex >= 0) {
                highlightCurrentInstruction(currentInstructionIndex);
            }
        }
    }

    /**
     * Load bytecode (or Lingo) for a handler.
     */
    public void loadHandlerBytecode(ScriptChunk script, ScriptChunk.Handler handler) {
        bytecodeModel.clear();
        currentInstructions.clear();
        currentInstructionIndex = -1;
        currentScriptId = script.id().value();
        currentHandlerName = script.getHandlerName(handler);
        currentScript = script;
        currentHandler = handler;

        if (showLingoView) {
            loadLingoView(script, handler);
        } else {
            loadBytecodeView(script, handler);
        }

        contextMenu.setCurrentScriptId(script.id().value());
        contextMenu.setCurrentHandlerName(currentHandlerName);
    }

    private void loadBytecodeView(ScriptChunk script, ScriptChunk.Handler handler) {
        for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
            String annotation = InstructionAnnotator.annotate(script, handler, instr, true);
            Breakpoint bp = controller != null ? controller.getBreakpoint(script.id().value(), currentHandlerName, instr.offset()) : null;

            InstructionDisplayItem item = new InstructionDisplayItem(
                instr.offset(),
                handler.instructions().indexOf(instr),
                instr.opcode().name(),
                instr.argument(),
                annotation,
                bp != null
            );
            item.setBreakpoint(bp);

            // Check if call target exists in the CCT (not a builtin)
            if (item.isCallInstruction() && navigator != null) {
                String targetName = item.getCallTargetName();
                if (targetName != null) {
                    HandlerNavigator.HandlerLocation location = navigator.findHandler(targetName);
                    item.setNavigable(location.found());
                }
            }

            bytecodeModel.addElement(item);
            currentInstructions.add(item);
        }
    }

    private void loadLingoView(ScriptChunk script, ScriptChunk.Handler handler) {
        ScriptNamesChunk names = null;
        if (script.file() != null) {
            names = script.file().getScriptNamesForScript(script);
            if (names == null) names = script.file().getScriptNames();
        }

        LingoDecompiler decompiler = new LingoDecompiler();
        LingoDecompiler.DecompiledHandler result =
            decompiler.decompileHandlerWithMapping(handler, script, names);

        for (int i = 0; i < result.lines().size(); i++) {
            LingoDecompiler.DecompiledLine line = result.lines().get(i);
            int offset = line.bytecodeOffset();

            Breakpoint bp = (controller != null && offset >= 0)
                ? controller.getBreakpoint(script.id().value(), currentHandlerName, offset)
                : null;

            // Create display item: use offset for breakpoint mapping, index for line number
            InstructionDisplayItem item = new InstructionDisplayItem(
                offset,       // bytecode offset (or -1 for structural lines)
                i,            // line index
                "",           // no opcode in Lingo mode
                0,            // no argument in Lingo mode
                line.text(),  // Lingo source line as annotation
                bp != null
            );
            item.setBreakpoint(bp);
            item.setLingoLine(true);

            bytecodeModel.addElement(item);
            currentInstructions.add(item);
        }
    }

    /**
     * Highlight the current instruction by bytecode index.
     */
    public void highlightCurrentInstruction(int bytecodeIndex) {
        if (showLingoView) {
            // In Lingo mode, find the line matching the bytecode offset
            int targetOffset = -1;
            if (currentHandler != null && bytecodeIndex >= 0
                && bytecodeIndex < currentHandler.instructions().size()) {
                targetOffset = currentHandler.instructions().get(bytecodeIndex).offset();
            }
            for (int i = 0; i < bytecodeModel.size(); i++) {
                InstructionDisplayItem item = bytecodeModel.get(i);
                boolean wasCurrent = item.isCurrent();
                item.setCurrent(item.getOffset() >= 0 && item.getOffset() == targetOffset);
                if (item.isCurrent() && !wasCurrent) {
                    currentInstructionIndex = bytecodeIndex;
                    bytecodeList.setSelectedIndex(i);
                    bytecodeList.ensureIndexIsVisible(i);
                }
            }
        } else {
            for (int i = 0; i < bytecodeModel.size(); i++) {
                InstructionDisplayItem item = bytecodeModel.get(i);
                boolean wasCurrent = item.isCurrent();
                item.setCurrent(item.getIndex() == bytecodeIndex);
                if (item.isCurrent() && !wasCurrent) {
                    currentInstructionIndex = bytecodeIndex;
                    bytecodeList.setSelectedIndex(i);
                    bytecodeList.ensureIndexIsVisible(i);
                }
            }
        }
        bytecodeList.repaint();
    }

    /**
     * Update breakpoint markers for all instructions.
     */
    public void refreshBreakpointMarkers() {
        if (controller != null && currentScriptId >= 0 && currentHandlerName != null) {
            for (int i = 0; i < bytecodeModel.size(); i++) {
                InstructionDisplayItem item = bytecodeModel.get(i);
                if (item.getOffset() < 0) continue; // structural line
                Breakpoint bp = controller.getBreakpoint(currentScriptId, currentHandlerName, item.getOffset());
                item.setBreakpoint(bp);
                item.setHasBreakpoint(bp != null);
            }
            bytecodeList.repaint();
        }
    }

    /**
     * Clear the bytecode display.
     */
    public void clear() {
        bytecodeModel.clear();
        currentInstructions.clear();
        currentInstructionIndex = -1;
        currentScriptId = -1;
        currentHandlerName = null;
        currentScript = null;
        currentHandler = null;
        bytecodeList.clearSelection();
    }

    /**
     * Set the debug controller.
     */
    public void setController(DebugController controller) {
        this.controller = controller;
        contextMenu.setController(controller);
    }

    /**
     * Set the handler navigator for checking if call targets exist.
     */
    public void setNavigator(HandlerNavigator navigator) {
        this.navigator = navigator;
    }

    /**
     * Set the current script ID.
     */
    public void setCurrentScriptId(int scriptId) {
        this.currentScriptId = scriptId;
        contextMenu.setCurrentScriptId(scriptId);
    }

    /**
     * Get the current script ID.
     */
    public int getCurrentScriptId() {
        return currentScriptId;
    }

    /**
     * Set the current handler name.
     */
    public void setCurrentHandlerName(String handlerName) {
        this.currentHandlerName = handlerName;
        contextMenu.setCurrentHandlerName(handlerName);
    }

    /**
     * Get the current handler name.
     */
    public String getCurrentHandlerName() {
        return currentHandlerName;
    }

    /**
     * Set the listener for bytecode list events.
     */
    public void setListener(BytecodeListListener listener) {
        this.listener = listener;
        contextMenu.setListener(new BytecodeContextMenu.BytecodeListListener() {
            @Override
            public void onBreakpointToggleRequested(int offset) {
                listener.onBreakpointToggleRequested(offset);
            }

            @Override
            public void onNavigateToHandler(String handlerName) {
                listener.onNavigateToHandler(handlerName);
            }
        });
    }

    /**
     * Get the bytecode list model.
     */
    public DefaultListModel<InstructionDisplayItem> getBytecodeModel() {
        return bytecodeModel;
    }

    /**
     * Get the bytecode list.
     */
    public JList<InstructionDisplayItem> getBytecodeList() {
        return bytecodeList;
    }

    /**
     * Get the current instruction index.
     */
    public int getCurrentInstructionIndex() {
        return currentInstructionIndex;
    }
}
