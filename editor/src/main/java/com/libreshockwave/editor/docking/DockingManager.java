package com.libreshockwave.editor.docking;

import com.libreshockwave.editor.panel.EditorPanel;

import javax.swing.*;
import javax.swing.plaf.basic.BasicInternalFrameUI;
import java.awt.*;
import java.awt.event.*;
import java.beans.PropertyVetoException;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Manages IDE-style docking using a recursive binary split tree.
 * <p>
 * The layout is a tree of {@link DockNode} objects:
 * <ul>
 *   <li>{@link DockSplit} — two children separated by a JSplitPane</li>
 *   <li>{@link DockLeaf} — JTabbedPane holding one or more docked panels</li>
 *   <li>{@link DockCenter} — the JDesktopPane (immutable, always one)</li>
 * </ul>
 * Multiple panels in the same position use tabs. Users can split any tab group
 * via right-click to create sub-zones (e.g., top-right and bottom-right).
 * <p>
 * All panel identity is by stable panelId, not by display title.
 */
public class DockingManager {

    public enum Edge { LEFT, RIGHT, TOP, BOTTOM }

    private final Map<String, EditorPanel> allPanels;

    // Tree state
    private DockNode root;
    private final DockCenter center;
    private final JPanel rootWrapper = new JPanel(new BorderLayout());

    // Fast lookup: panelId → which leaf it's in
    private final Map<String, DockLeaf> panelLeaves = new LinkedHashMap<>();

    // Saved state for undocking
    private final Map<String, Container> savedContent = new LinkedHashMap<>();
    private final Map<String, Rectangle> savedBounds = new LinkedHashMap<>();

    // Snap overlay for drag-to-dock
    private final SnapOverlay snapOverlay;
    private Edge pendingSnap;

    // Suppress save during compound operations
    private boolean suppressSave = false;

    private static final int SNAP_TRIGGER_MARGIN = 24;
    private static final double SNAP_PREVIEW_FRACTION = 0.18;

    public DockingManager(JFrame frame, JDesktopPane desktop, Map<String, EditorPanel> allPanels) {
        this.allPanels = allPanels;
        this.center = new DockCenter(desktop);
        this.center.setManager(this);
        this.root = center;
        rootWrapper.add(root.getComponent(), BorderLayout.CENTER);

        // Snap overlay on desktop's drag layer
        snapOverlay = new SnapOverlay();
        snapOverlay.setVisible(false);
        desktop.add(snapOverlay, JLayeredPane.DRAG_LAYER);
        desktop.addComponentListener(new ComponentAdapter() {
            @Override
            public void componentResized(ComponentEvent e) {
                snapOverlay.setBounds(0, 0, desktop.getWidth(), desktop.getHeight());
            }
        });

        // Custom desktop manager for drag-to-dock
        desktop.setDesktopManager(new DockingDesktopManager());

        // Right-click menus on floating panel title bars
        for (EditorPanel panel : allPanels.values()) {
            installTitleBarMenu(panel);
        }
    }

    /** Returns the root component to add to the frame's content area. */
    public JComponent getComponent() {
        return rootWrapper;
    }

    /** Get the root node (for serialization). */
    public DockNode getRoot() {
        return root;
    }

    /** Save the current layout to disk. */
    public void saveLayout() {
        LayoutPersistence.save(root);
    }

    private void autoSave() {
        if (!suppressSave) saveLayout();
    }

    /** Load a saved layout from disk. Returns true if a layout was loaded. */
    public boolean loadLayout() {
        return LayoutPersistence.load(this);
    }

    // ---- Public API ----

    /** Dock a panel as a center tab (alongside the desktop/stage). */
    public void dockCenter(String panelId) {
        EditorPanel panel = allPanels.get(panelId);
        if (panel == null) return;
        if (isDocked(panelId)) undock(panelId);

        Container content = extractContent(panel);
        center.addTab(panelId, panel.getTitle(), content);
        // Track in panelLeaves as null to indicate center-docked
        panelLeaves.put(panelId, null);
        autoSave();
    }

    /** Dock a panel to a desktop edge. Adds as tab if a zone already exists there. */
    public void dockAtEdge(String panelId, Edge edge) {
        EditorPanel panel = allPanels.get(panelId);
        if (panel == null) return;
        suppressSave = true;
        if (isDocked(panelId)) undock(panelId);
        suppressSave = false;

        Container content = extractContent(panel);

        // Check if there's already a leaf adjacent to the center at this edge
        DockLeaf adjacent = findAdjacentLeaf(edge);
        if (adjacent != null) {
            adjacent.addTab(panelId, panel.getTitle(), content);
            panelLeaves.put(panelId, adjacent);
            autoSave();
            return;
        }

        // Create new leaf and split around the center (or its nearest ancestor)
        DockLeaf newLeaf = createLeaf();
        newLeaf.addTab(panelId, panel.getTitle(), content);
        panelLeaves.put(panelId, newLeaf);

        DockNode target = center;
        int orientation;
        boolean newFirst;
        double fraction;

        switch (edge) {
            case LEFT -> { orientation = JSplitPane.HORIZONTAL_SPLIT; newFirst = true; fraction = 0.15; }
            case RIGHT -> { orientation = JSplitPane.HORIZONTAL_SPLIT; newFirst = false; fraction = 0.85; }
            case TOP -> { orientation = JSplitPane.VERTICAL_SPLIT; newFirst = true; fraction = 0.15; }
            case BOTTOM -> { orientation = JSplitPane.VERTICAL_SPLIT; newFirst = false; fraction = 0.70; }
            default -> { return; }
        }

        DockSplit split = newFirst
            ? new DockSplit(newLeaf, target, orientation, fraction)
            : new DockSplit(target, newLeaf, orientation, fraction);

        replaceNode(target, split);
        autoSave();
    }

    /** Dock a panel to a desktop edge, always creating a new split (never tabs). */
    public void dockAtEdgeNew(String panelId, Edge edge) {
        EditorPanel panel = allPanels.get(panelId);
        if (panel == null) return;
        suppressSave = true;
        if (panelLeaves.containsKey(panelId)) undock(panelId);
        suppressSave = false;

        Container content = extractContent(panel);

        // If there's an adjacent leaf, split it instead of tabbing
        DockLeaf adjacent = findAdjacentLeaf(edge);
        if (adjacent != null) {
            DockLeaf newLeaf = createLeaf();
            newLeaf.addTab(panelId, panel.getTitle(), content);
            panelLeaves.put(panelId, newLeaf);

            int splitOrientation = (edge == Edge.LEFT || edge == Edge.RIGHT)
                ? JSplitPane.VERTICAL_SPLIT : JSplitPane.HORIZONTAL_SPLIT;

            DockSplit split = new DockSplit(adjacent, newLeaf, splitOrientation, 0.5);
            replaceNode(adjacent, split);
            autoSave();
            return;
        }

        // No existing zone — same as dockAtEdge
        DockLeaf newLeaf = createLeaf();
        newLeaf.addTab(panelId, panel.getTitle(), content);
        panelLeaves.put(panelId, newLeaf);

        DockNode target = center;
        int orientation;
        boolean newFirst;
        double fraction;

        switch (edge) {
            case LEFT -> { orientation = JSplitPane.HORIZONTAL_SPLIT; newFirst = true; fraction = 0.15; }
            case RIGHT -> { orientation = JSplitPane.HORIZONTAL_SPLIT; newFirst = false; fraction = 0.85; }
            case TOP -> { orientation = JSplitPane.VERTICAL_SPLIT; newFirst = true; fraction = 0.15; }
            case BOTTOM -> { orientation = JSplitPane.VERTICAL_SPLIT; newFirst = false; fraction = 0.70; }
            default -> { return; }
        }

        DockSplit newSplit = newFirst
            ? new DockSplit(newLeaf, target, orientation, fraction)
            : new DockSplit(target, newLeaf, orientation, fraction);

        replaceNode(target, newSplit);
        autoSave();
    }

    /** Dock a panel adjacent to a specific leaf (creates a split). */
    public void dockAt(String panelId, DockLeaf targetLeaf, Edge direction) {
        EditorPanel panel = allPanels.get(panelId);
        if (panel == null) return;
        suppressSave = true;
        if (panelLeaves.containsKey(panelId)) undock(panelId);
        suppressSave = false;

        Container content = extractContent(panel);

        DockLeaf newLeaf = createLeaf();
        newLeaf.addTab(panelId, panel.getTitle(), content);
        panelLeaves.put(panelId, newLeaf);

        int orientation = (direction == Edge.LEFT || direction == Edge.RIGHT)
            ? JSplitPane.HORIZONTAL_SPLIT : JSplitPane.VERTICAL_SPLIT;

        boolean newFirst = (direction == Edge.LEFT || direction == Edge.TOP);

        DockSplit split = newFirst
            ? new DockSplit(newLeaf, targetLeaf, orientation, 0.5)
            : new DockSplit(targetLeaf, newLeaf, orientation, 0.5);

        replaceNode(targetLeaf, split);
        autoSave();
    }

    /** Split a tab out of a leaf into a new adjacent leaf. */
    public void splitTab(DockLeaf leaf, String panelId, Edge direction) {
        if (leaf.getTabCount() <= 1) return;

        int idx = leaf.getPanelIds().indexOf(panelId);
        if (idx < 0) return;
        Container content = leaf.getContentAt(idx);
        leaf.removeTab(panelId);

        EditorPanel panel = allPanels.get(panelId);
        String displayTitle = panel != null ? panel.getTitle() : panelId;

        DockLeaf newLeaf = createLeaf();
        newLeaf.addTab(panelId, displayTitle, content);
        panelLeaves.put(panelId, newLeaf);

        int orientation = (direction == Edge.LEFT || direction == Edge.RIGHT)
            ? JSplitPane.HORIZONTAL_SPLIT : JSplitPane.VERTICAL_SPLIT;

        boolean newFirst = (direction == Edge.LEFT || direction == Edge.TOP);

        DockSplit split = newFirst
            ? new DockSplit(newLeaf, leaf, orientation, 0.5)
            : new DockSplit(leaf, newLeaf, orientation, 0.5);

        replaceNode(leaf, split);
        autoSave();
    }

    /** Undock a panel, restoring its content to the JInternalFrame. Does NOT show it. */
    public void undock(String panelId) {
        if (!panelLeaves.containsKey(panelId)) return;

        DockLeaf leaf = panelLeaves.remove(panelId);

        if (leaf == null) {
            // Center-docked panel
            Container content = center.removeTab(panelId);
            if (content != null) {
                restoreContent(panelId, content);
            }
            return;
        }

        // Normal leaf-docked panel
        int idx = leaf.getPanelIds().indexOf(panelId);
        Container content = (idx >= 0) ? leaf.getContentAt(idx) : null;
        leaf.removeTab(panelId);

        if (content != null) {
            restoreContent(panelId, content);
        }

        if (leaf.isEmpty()) {
            collapseEmptyLeaf(leaf);
        }
        autoSave();
    }

    /** Undock and show the panel as a floating window. */
    public void undockAndShow(String panelId) {
        undock(panelId);
        EditorPanel panel = allPanels.get(panelId);
        if (panel != null) {
            panel.setVisible(true);
            try { panel.setSelected(true); } catch (PropertyVetoException ignored) {}
        }
    }

    public boolean isDocked(String panelId) {
        return panelLeaves.containsKey(panelId);
    }

    /** Toggle visibility, handling both docked and floating states. */
    public void togglePanel(String panelId, boolean visible) {
        if (panelLeaves.containsKey(panelId)) {
            if (visible) {
                DockLeaf leaf = panelLeaves.get(panelId);
                if (leaf != null) {
                    leaf.selectTab(panelId);
                } else {
                    center.selectTab(panelId);
                }
            } else {
                undock(panelId);
                EditorPanel panel = allPanels.get(panelId);
                if (panel != null) panel.setVisible(false);
            }
        } else {
            EditorPanel panel = allPanels.get(panelId);
            if (panel != null) {
                panel.setVisible(visible);
                if (visible) {
                    try { panel.setSelected(true); } catch (PropertyVetoException ignored) {}
                }
            }
        }
    }

    /** Undock all panels and restore them to floating. */
    public void undockAll() {
        suppressSave = true;
        for (String panelId : panelLeaves.keySet().toArray(new String[0])) {
            undockAndShow(panelId);
        }
        suppressSave = false;
        autoSave();
    }

    /** Set up a default IDE-style docked layout. */
    public void applyDefaultDockedLayout() {
        undockAll();

        // Bottom: tabbed together
        dockAtEdge("score", Edge.BOTTOM);
        dockAtEdge("cast", Edge.BOTTOM);
        dockAtEdge("script", Edge.BOTTOM);
        dockAtEdge("message", Edge.BOTTOM);

        // Left: tool palette
        dockAtEdge("tool-palette", Edge.LEFT);

        // Right: property inspector
        dockAtEdge("property-inspector", Edge.RIGHT);

        // Split right zone: debugger below inspector
        DockLeaf rightLeaf = panelLeaves.get("property-inspector");
        if (rightLeaf != null) {
            dockAt("bytecode-debugger", rightLeaf, Edge.BOTTOM);
        }

        // Show Stage floating in center
        EditorPanel stage = allPanels.get("stage");
        if (stage != null) {
            stage.setVisible(true);
            stage.setBounds(10, 10, 660, 500);
            try { stage.setSelected(true); } catch (PropertyVetoException ignored) {}
        }
    }

    // ---- Tree operations ----

    private DockSplit findParent(DockNode target) {
        return findParentRec(root, target);
    }

    private DockSplit findParentRec(DockNode current, DockNode target) {
        if (current instanceof DockSplit split) {
            if (split.getFirst() == target || split.getSecond() == target) return split;
            DockSplit result = findParentRec(split.getFirst(), target);
            if (result != null) return result;
            return findParentRec(split.getSecond(), target);
        }
        return null;
    }

    private void replaceNode(DockNode oldNode, DockNode newNode) {
        if (root == oldNode) {
            setRoot(newNode);
            return;
        }
        DockSplit parent = findParent(oldNode);
        if (parent != null) {
            if (parent.getFirst() == oldNode) {
                parent.setFirst(newNode);
            } else {
                parent.setSecond(newNode);
            }
        }
    }

    private void setRoot(DockNode newRoot) {
        rootWrapper.removeAll();
        root = newRoot;
        rootWrapper.add(root.getComponent(), BorderLayout.CENTER);
        rootWrapper.revalidate();
        rootWrapper.repaint();
    }

    /**
     * Walk up from center looking for an adjacent DockLeaf at the given edge.
     */
    private DockLeaf findAdjacentLeaf(Edge edge) {
        DockNode node = center;
        while (true) {
            DockSplit parent = findParent(node);
            if (parent == null) return null;

            switch (edge) {
                case LEFT -> {
                    if (parent.getOrientation() == JSplitPane.HORIZONTAL_SPLIT
                            && parent.getSecond() == node
                            && parent.getFirst() instanceof DockLeaf leaf) {
                        return leaf;
                    }
                }
                case RIGHT -> {
                    if (parent.getOrientation() == JSplitPane.HORIZONTAL_SPLIT
                            && parent.getFirst() == node
                            && parent.getSecond() instanceof DockLeaf leaf) {
                        return leaf;
                    }
                }
                case TOP -> {
                    if (parent.getOrientation() == JSplitPane.VERTICAL_SPLIT
                            && parent.getSecond() == node
                            && parent.getFirst() instanceof DockLeaf leaf) {
                        return leaf;
                    }
                }
                case BOTTOM -> {
                    if (parent.getOrientation() == JSplitPane.VERTICAL_SPLIT
                            && parent.getFirst() == node
                            && parent.getSecond() instanceof DockLeaf leaf) {
                        return leaf;
                    }
                }
            }

            node = parent; // keep walking up
        }
    }

    private void collapseEmptyLeaf(DockLeaf leaf) {
        DockSplit parent = findParent(leaf);
        if (parent == null) {
            setRoot(center);
            return;
        }

        DockNode sibling = (parent.getFirst() == leaf) ? parent.getSecond() : parent.getFirst();
        replaceNode(parent, sibling);
    }

    // ---- Content extraction/restoration ----

    private Container extractContent(EditorPanel panel) {
        savedBounds.put(panel.getPanelId(), panel.getBounds());
        Container content = panel.getContentPane();
        savedContent.put(panel.getPanelId(), content);
        panel.setContentPane(new JPanel());
        panel.setVisible(false);
        return content;
    }

    private void restoreContent(String panelId, Container content) {
        EditorPanel panel = allPanels.get(panelId);
        if (panel == null) return;

        panel.setContentPane(content);

        savedContent.remove(panelId);
        Rectangle bounds = savedBounds.remove(panelId);
        if (bounds != null) panel.setBounds(bounds);
    }

    private DockLeaf createLeaf() {
        DockLeaf leaf = new DockLeaf();
        leaf.setManager(this);
        leaf.installContextMenu();
        return leaf;
    }

    // ---- Tab context menu (called from DockLeaf) ----

    void showTabContextMenu(DockLeaf leaf, String panelId, JTabbedPane tabs, int x, int y) {
        EditorPanel panel = allPanels.get(panelId);
        String displayName = panel != null ? panel.getTitle() : panelId;

        JPopupMenu menu = new JPopupMenu();

        JMenuItem floatItem = new JMenuItem("Float");
        floatItem.addActionListener(e -> undockAndShow(panelId));
        menu.add(floatItem);

        menu.addSeparator();

        // Split options (only if leaf has multiple tabs)
        boolean canSplit = leaf.getTabCount() > 1;

        JMenuItem splitLeft = new JMenuItem("Split Left");
        splitLeft.setEnabled(canSplit);
        splitLeft.addActionListener(e -> splitTab(leaf, panelId, Edge.LEFT));
        menu.add(splitLeft);

        JMenuItem splitRight = new JMenuItem("Split Right");
        splitRight.setEnabled(canSplit);
        splitRight.addActionListener(e -> splitTab(leaf, panelId, Edge.RIGHT));
        menu.add(splitRight);

        JMenuItem splitUp = new JMenuItem("Split Up");
        splitUp.setEnabled(canSplit);
        splitUp.addActionListener(e -> splitTab(leaf, panelId, Edge.TOP));
        menu.add(splitUp);

        JMenuItem splitDown = new JMenuItem("Split Down");
        splitDown.setEnabled(canSplit);
        splitDown.addActionListener(e -> splitTab(leaf, panelId, Edge.BOTTOM));
        menu.add(splitDown);

        menu.addSeparator();

        // Move to edge options (tabs with existing zone)
        for (Edge edge : Edge.values()) {
            JMenuItem moveItem = new JMenuItem("Move to " + edgeName(edge));
            moveItem.addActionListener(e -> {
                undock(panelId);
                dockAtEdge(panelId, edge);
            });
            menu.add(moveItem);
        }

        JMenuItem moveCenterItem = new JMenuItem("Move to Center");
        moveCenterItem.addActionListener(e -> {
            undock(panelId);
            dockCenter(panelId);
        });
        menu.add(moveCenterItem);

        menu.addSeparator();

        // Move to edge as new split (side by side with existing zone)
        for (Edge edge : Edge.values()) {
            JMenuItem moveItem = new JMenuItem("Move to " + edgeName(edge) + " (New Split)");
            moveItem.addActionListener(e -> {
                undock(panelId);
                dockAtEdgeNew(panelId, edge);
            });
            menu.add(moveItem);
        }

        menu.show(tabs, x, y);
    }

    // ---- Floating panel title bar context menu ----

    private void installTitleBarMenu(EditorPanel panel) {
        try {
            JComponent titleBar = ((BasicInternalFrameUI) panel.getUI()).getNorthPane();
            if (titleBar == null) return;

            titleBar.addMouseListener(new MouseAdapter() {
                @Override public void mousePressed(MouseEvent e) { popup(e); }
                @Override public void mouseReleased(MouseEvent e) { popup(e); }

                private void popup(MouseEvent e) {
                    if (!e.isPopupTrigger()) return;
                    String id = panel.getPanelId();
                    JPopupMenu menu = new JPopupMenu();
                    for (Edge edge : Edge.values()) {
                        JMenuItem item = new JMenuItem("Dock " + edgeName(edge));
                        item.addActionListener(ev -> dockAtEdge(id, edge));
                        menu.add(item);
                    }
                    JMenuItem centerItem = new JMenuItem("Dock Center");
                    centerItem.addActionListener(ev -> dockCenter(id));
                    menu.add(centerItem);
                    menu.addSeparator();
                    for (Edge edge : Edge.values()) {
                        JMenuItem item = new JMenuItem("Dock " + edgeName(edge) + " (New Split)");
                        item.addActionListener(ev -> dockAtEdgeNew(id, edge));
                        menu.add(item);
                    }
                    menu.show(titleBar, e.getX(), e.getY());
                }
            });
        } catch (ClassCastException ignored) {
            // Non-basic L&F — skip
        }
    }

    private static String edgeName(Edge e) {
        return e.name().charAt(0) + e.name().substring(1).toLowerCase();
    }

    // ---- Snap overlay ----

    private class SnapOverlay extends JComponent {
        private Edge edge;

        void setEdge(Edge e) {
            this.edge = e;
            setVisible(e != null);
            repaint();
        }

        @Override
        protected void paintComponent(Graphics g) {
            if (edge == null) return;
            Graphics2D g2 = (Graphics2D) g.create();
            int w = getWidth(), h = getHeight();
            int previewWidth = Math.max(80, (int) Math.round(w * SNAP_PREVIEW_FRACTION));
            int previewHeight = Math.max(70, (int) Math.round(h * SNAP_PREVIEW_FRACTION));

            Rectangle r = switch (edge) {
                case LEFT -> new Rectangle(0, 0, previewWidth, h);
                case RIGHT -> new Rectangle(w - previewWidth, 0, previewWidth, h);
                case TOP -> new Rectangle(0, 0, w, previewHeight);
                case BOTTOM -> new Rectangle(0, h - previewHeight, w, previewHeight);
            };

            g2.setColor(new Color(0, 120, 215, 50));
            g2.fillRect(r.x, r.y, r.width, r.height);
            g2.setColor(new Color(0, 120, 215, 160));
            g2.setStroke(new BasicStroke(2));
            g2.drawRect(r.x + 1, r.y + 1, r.width - 3, r.height - 3);
            g2.dispose();
        }
    }

    // ---- Custom desktop manager for drag-to-dock ----

    private class DockingDesktopManager extends DefaultDesktopManager {

        @Override
        public void beginDraggingFrame(JComponent f) {
            super.beginDraggingFrame(f);
            pendingSnap = null;
        }

        @Override
        public void dragFrame(JComponent f, int newX, int newY) {
            super.dragFrame(f, newX, newY);

            int dw = center.getDesktop().getWidth();
            int dh = center.getDesktop().getHeight();
            int fw = f.getWidth();
            int fh = f.getHeight();

            Edge snap = null;
            if (newX < SNAP_TRIGGER_MARGIN) {
                snap = Edge.LEFT;
            } else if (newX + fw > dw - SNAP_TRIGGER_MARGIN) {
                snap = Edge.RIGHT;
            } else if (newY < SNAP_TRIGGER_MARGIN) {
                snap = Edge.TOP;
            } else if (newY + fh > dh - SNAP_TRIGGER_MARGIN) {
                snap = Edge.BOTTOM;
            }

            if (snap != pendingSnap) {
                pendingSnap = snap;
                snapOverlay.setEdge(snap);
            }
        }

        @Override
        public void endDraggingFrame(JComponent f) {
            super.endDraggingFrame(f);

            if (pendingSnap != null && f instanceof EditorPanel panel) {
                Edge edge = pendingSnap;
                pendingSnap = null;
                snapOverlay.setEdge(null);
                SwingUtilities.invokeLater(() -> dockAtEdge(panel.getPanelId(), edge));
            } else {
                pendingSnap = null;
                snapOverlay.setEdge(null);
            }
        }
    }
}
