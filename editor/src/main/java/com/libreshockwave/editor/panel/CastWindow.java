package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.EditorFrame;
import com.libreshockwave.editor.cast.CastGridPanel;
import com.libreshockwave.editor.cast.CastListPanel;
import com.libreshockwave.editor.cast.CastThumbnailRenderer;
import com.libreshockwave.editor.extraction.ExportHandler;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.model.MemberNodeData;
import com.libreshockwave.editor.scanning.FileProcessor;
import com.libreshockwave.editor.selection.SelectionEvent;

import javax.swing.*;
import java.awt.*;
import java.awt.event.ComponentAdapter;
import java.awt.event.ComponentEvent;
import java.awt.event.InputEvent;
import java.awt.event.KeyEvent;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.image.BufferedImage;
import java.lang.ref.SoftReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Cast window - Director MX 2004 cast member browser.
 * Supports grid view (thumbnails) and list view with search/filter, context menu export,
 * and cast library tabs. Grid view is the default, matching Director MX 2004.
 */
public class CastWindow extends EditorPanel {

    private final JTabbedPane castTabs;
    private final JTextField searchField;
    private final JComboBox<String> typeFilter;
    private final JLabel statusLabel;

    private boolean gridView = true;
    private List<CastMemberInfo> allMembers = new ArrayList<>();
    private List<CastMemberInfo> filteredMembers = new ArrayList<>();
    private final Set<Integer> selectedMemberNums = new LinkedHashSet<>();

    private final Map<Integer, SoftReference<BufferedImage>> thumbnailCache = new HashMap<>();
    private SwingWorker<Void, ThumbnailResult> thumbnailWorker;

    public CastWindow(EditorContext context) {
        super("cast", "Cast", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Toolbar with view toggle, search, and filter
        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);

        JButton gridViewBtn = new JButton("Grid");
        JButton listViewBtn = new JButton("List");
        gridViewBtn.setFont(gridViewBtn.getFont().deriveFont(Font.BOLD));
        gridViewBtn.addActionListener(e -> {
            gridView = true;
            gridViewBtn.setFont(gridViewBtn.getFont().deriveFont(Font.BOLD));
            listViewBtn.setFont(listViewBtn.getFont().deriveFont(Font.PLAIN));
            rebuildView();
        });
        listViewBtn.addActionListener(e -> {
            gridView = false;
            listViewBtn.setFont(listViewBtn.getFont().deriveFont(Font.BOLD));
            gridViewBtn.setFont(gridViewBtn.getFont().deriveFont(Font.PLAIN));
            rebuildView();
        });
        toolbar.add(gridViewBtn);
        toolbar.add(listViewBtn);
        toolbar.addSeparator();

        toolbar.add(new JLabel(" Search: "));
        searchField = new JTextField(10);
        searchField.addActionListener(e -> applyFilterAndRebuild());
        toolbar.add(searchField);

        toolbar.addSeparator();
        toolbar.add(new JLabel(" Type: "));
        typeFilter = new JComboBox<>(getTypeFilterItems());
        typeFilter.addActionListener(e -> applyFilterAndRebuild());
        toolbar.add(typeFilter);

        // Cast library tabs (bottom tabs like Director MX 2004)
        castTabs = new JTabbedPane(JTabbedPane.BOTTOM);
        castTabs.addTab("Internal", new JLabel("No movie loaded", SwingConstants.CENTER));

        // Status bar
        statusLabel = new JLabel(" Ready");
        statusLabel.setBorder(BorderFactory.createLoweredBevelBorder());

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(castTabs, BorderLayout.CENTER);
        panel.add(statusLabel, BorderLayout.SOUTH);

        setContentPane(panel);
        setSize(400, 350);
    }

    private String[] getTypeFilterItems() {
        return new String[]{
            "All Types", "Bitmap", "Script", "Sound", "Text", "Button",
            "Shape", "Film Loop", "Palette", "Field", "Transition"
        };
    }

    @Override
    protected void onFileOpened(DirectorFile file) {
        FileProcessor processor = new FileProcessor();
        allMembers = processor.processMembers(file);
        filteredMembers = new ArrayList<>(allMembers);
        rebuildView();
        statusLabel.setText(" " + allMembers.size() + " members");
    }

    @Override
    protected void onFileClosed() {
        cancelThumbnailWorker();
        thumbnailCache.clear();
        allMembers.clear();
        filteredMembers.clear();
        selectedMemberNums.clear();
        castTabs.removeAll();
        castTabs.addTab("Internal", new JLabel("No movie loaded", SwingConstants.CENTER));
        statusLabel.setText(" Ready");
        searchField.setText("");
        typeFilter.setSelectedIndex(0);
    }

    private void applyFilterAndRebuild() {
        String searchText = searchField.getText().toLowerCase().trim();
        String selectedType = (String) typeFilter.getSelectedItem();

        filteredMembers = new ArrayList<>();
        for (CastMemberInfo info : allMembers) {
            // Type filter
            if (!"All Types".equals(selectedType)) {
                String typeName = info.memberType().getName();
                if (!typeName.equalsIgnoreCase(selectedType)) continue;
            }
            // Search filter
            if (!searchText.isEmpty()) {
                String name = info.name().toLowerCase();
                String details = info.details().toLowerCase();
                if (!name.contains(searchText) && !details.contains(searchText)) continue;
            }
            filteredMembers.add(info);
        }

        selectedMemberNums.retainAll(filteredMembers.stream()
            .map(CastMemberInfo::memberNum)
            .collect(java.util.stream.Collectors.toSet()));
        rebuildView();
        statusLabel.setText(" " + filteredMembers.size() + " of " + allMembers.size() + " members");
    }

    private void rebuildView() {
        cancelThumbnailWorker();
        castTabs.removeAll();

        if (filteredMembers.isEmpty() && allMembers.isEmpty()) {
            castTabs.addTab("Internal", new JLabel("No movie loaded", SwingConstants.CENTER));
            return;
        }

        if (gridView) {
            castTabs.addTab("Internal", buildGridView());
        } else {
            castTabs.addTab("Internal", buildListView());
        }
    }

    private JScrollPane buildGridView() {
        CastGridPanel grid = new CastGridPanel();
        Map<Integer, JLabel> thumbnailLabels = new HashMap<>();

        for (CastMemberInfo info : filteredMembers) {
            JLabel thumbLabel = grid.addMemberCell(info);
            if (info.memberType() == MemberType.BITMAP) {
                // Check cache first
                SoftReference<BufferedImage> cached = thumbnailCache.get(info.memberNum());
                if (cached != null && cached.get() != null) {
                    thumbLabel.setIcon(new ImageIcon(cached.get()));
                } else {
                    thumbnailLabels.put(info.memberNum(), thumbLabel);
                }
            }
        }

        // Click handler for selection and context menu
        grid.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                handleGridClick(grid, e);
            }
            @Override
            public void mouseReleased(MouseEvent e) {
                if (e.isPopupTrigger()) handleGridClick(grid, e);
            }
        });
        installSelectAllAction(grid, () -> {
            selectAllFilteredMembers();
            updateGridSelectionVisuals(grid);
        });
        updateGridSelectionVisuals(grid);

        JScrollPane sp = new JScrollPane(grid);
        sp.setHorizontalScrollBarPolicy(ScrollPaneConstants.HORIZONTAL_SCROLLBAR_NEVER);
        sp.getVerticalScrollBar().setUnitIncrement(16);

        // Revalidate grid when viewport is resized so rows re-wrap
        sp.getViewport().addComponentListener(new ComponentAdapter() {
            @Override
            public void componentResized(ComponentEvent e) {
                grid.revalidate();
            }
        });

        // Launch background bitmap decoding if there are bitmaps to decode
        if (!thumbnailLabels.isEmpty()) {
            launchThumbnailWorker(thumbnailLabels);
        }

        return sp;
    }

    private void launchThumbnailWorker(Map<Integer, JLabel> thumbnailLabels) {
        DirectorFile dirFile = context.getFile();
        if (dirFile == null) return;

        // Collect bitmap members that need decoding
        List<CastMemberInfo> bitmapMembers = new ArrayList<>();
        for (CastMemberInfo info : filteredMembers) {
            if (info.memberType() == MemberType.BITMAP && thumbnailLabels.containsKey(info.memberNum())) {
                bitmapMembers.add(info);
            }
        }

        thumbnailWorker = new SwingWorker<>() {
            @Override
            protected Void doInBackground() {
                for (CastMemberInfo info : bitmapMembers) {
                    if (isCancelled()) break;
                    try {
                        dirFile.decodeBitmap(info.member()).ifPresent(bitmap -> {
                            BufferedImage fullImage = bitmap.toBufferedImage();
                            BufferedImage thumb = CastThumbnailRenderer.createBitmapThumbnail(fullImage, 48);
                            thumbnailCache.put(info.memberNum(), new SoftReference<>(thumb));
                            publish(new ThumbnailResult(info.memberNum(), thumb));
                        });
                    } catch (Exception e) {
                        // Skip members that fail to decode
                    }
                }
                return null;
            }

            @Override
            protected void process(List<ThumbnailResult> results) {
                for (ThumbnailResult result : results) {
                    JLabel label = thumbnailLabels.get(result.memberNum);
                    if (label != null) {
                        label.setIcon(new ImageIcon(result.thumbnail));
                    }
                }
            }
        };
        thumbnailWorker.execute();
    }

    private void cancelThumbnailWorker() {
        if (thumbnailWorker != null && !thumbnailWorker.isDone()) {
            thumbnailWorker.cancel(true);
            thumbnailWorker = null;
        }
    }

    private void handleGridClick(CastGridPanel grid, MouseEvent e) {
        JPanel cell = findGridCellAt(grid, e.getPoint());
        if (cell == null) return;

        Object idxValue = cell.getClientProperty("memberIndex");
        if (!(idxValue instanceof Integer idx) || idx < 0 || idx >= filteredMembers.size()) return;

        CastMemberInfo info = filteredMembers.get(idx);
        if (e.isPopupTrigger()) {
            if (!selectedMemberNums.contains(info.memberNum())) {
                selectSingleMember(info);
                updateGridSelectionVisuals(grid);
            }
            showContextMenu(grid, e, info);
            return;
        }

        grid.requestFocusInWindow();
        if ((e.getModifiersEx() & InputEvent.CTRL_DOWN_MASK) != 0) {
            toggleSelectedMember(info);
        } else {
            selectSingleMember(info);
            openMemberEditor(info);
        }
        updateGridSelectionVisuals(grid);
    }

    private JScrollPane buildListView() {
        CastListPanel listPanel = new CastListPanel();

        for (CastMemberInfo info : filteredMembers) {
            listPanel.addMember(info.memberNum(), info.name(), info.memberType().getName());
        }

        // Wire selection events from internal JList
        JList<String> jList = listPanel.getList();
        jList.setSelectionMode(ListSelectionModel.MULTIPLE_INTERVAL_SELECTION);
        applyListSelection(jList);
        jList.addListSelectionListener(e -> {
            if (!e.getValueIsAdjusting()) {
                syncSelectionFromList(jList);
            }
        });
        installSelectAllAction(jList, () -> {
            selectAllFilteredMembers();
            applyListSelection(jList);
        });
        jList.getInputMap(JComponent.WHEN_FOCUSED).put(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, 0), "open-selected-member");
        jList.getActionMap().put("open-selected-member", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                int idx = jList.getLeadSelectionIndex();
                if (idx >= 0 && idx < filteredMembers.size()) {
                    openMemberEditor(filteredMembers.get(idx));
                }
            }
        });
        jList.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
                if (SwingUtilities.isLeftMouseButton(e) && e.getClickCount() == 2) {
                    int idx = jList.locationToIndex(e.getPoint());
                    if (idx >= 0 && idx < filteredMembers.size()) {
                        openMemberEditor(filteredMembers.get(idx));
                    }
                }
            }
        });

        // Right-click context menu
        jList.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                if (e.isPopupTrigger()) showListPopup(jList, e);
            }
            @Override
            public void mouseReleased(MouseEvent e) {
                if (e.isPopupTrigger()) showListPopup(jList, e);
            }
        });

        JScrollPane sp = new JScrollPane(listPanel);
        sp.getVerticalScrollBar().setUnitIncrement(16);
        return sp;
    }

    private void showListPopup(JList<String> jList, MouseEvent e) {
        int idx = jList.locationToIndex(e.getPoint());
        if (idx >= 0 && idx < filteredMembers.size()) {
            CastMemberInfo info = filteredMembers.get(idx);
            if (!selectedMemberNums.contains(info.memberNum())) {
                selectSingleMember(info);
                applyListSelection(jList);
            }
            showContextMenu(jList, e, info);
        }
    }

    private void openMemberEditor(CastMemberInfo info) {
        EditorFrame editorFrame = getEditorFrame();
        if (editorFrame == null) return;

        // Map member type to the right editor window by panelId
        String panelId = switch (info.memberType()) {
            case BITMAP, PICTURE -> "paint";
            case TEXT, RICH_TEXT, BUTTON -> "text";
            case SCRIPT -> "script";
            case SOUND -> "sound";
            case SHAPE -> "vector-shape";
            default -> null;
        };
        if (panelId == null) return;

        // Show the panel (handles docked/floating/hidden)
        editorFrame.showPanel(panelId);

        // Load the member into the panel
        EditorPanel panel = editorFrame.getPanel(panelId);
        if (panel instanceof PaintWindow pw) pw.loadMember(info);
        else if (panel instanceof TextEditorWindow tw) tw.loadMember(info);
        else if (panel instanceof FieldEditorWindow fw) fw.loadMember(info);
        else if (panel instanceof ScriptEditorWindow sw) sw.loadMember(info);
        else if (panel instanceof SoundWindow sow) sow.loadMember(info);
    }

    private EditorFrame getEditorFrame() {
        Window w = SwingUtilities.getWindowAncestor(this);
        return w instanceof EditorFrame ef ? ef : null;
    }

    private void showContextMenu(Component parent, MouseEvent e, CastMemberInfo info) {
        JPopupMenu popup = new JPopupMenu();

        JMenuItem exportItem = new JMenuItem("Export...");
        exportItem.addActionListener(ev -> exportMember(info));
        popup.add(exportItem);

        JMenuItem copyName = new JMenuItem("Copy Name");
        copyName.addActionListener(ev -> {
            java.awt.datatransfer.StringSelection sel =
                new java.awt.datatransfer.StringSelection(info.name());
            Toolkit.getDefaultToolkit().getSystemClipboard().setContents(sel, null);
        });
        popup.add(copyName);

        popup.show(parent, e.getX(), e.getY());
    }

    private void exportMember(CastMemberInfo info) {
        DirectorFile dirFile = context.getFile();
        if (dirFile == null) return;

        String filePath = context.getCurrentPath() != null ? context.getCurrentPath().toString() : "";
        MemberNodeData memberData = new MemberNodeData(filePath, info);

        ExportHandler handler = new ExportHandler();
        handler.setStatusCallback(statusLabel::setText);

        JFrame parentFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
        handler.export(parentFrame, dirFile, memberData, "");
    }

    private void installSelectAllAction(JComponent component, Runnable action) {
        KeyStroke keyStroke = KeyStroke.getKeyStroke(KeyEvent.VK_A, InputEvent.CTRL_DOWN_MASK);
        component.getInputMap(JComponent.WHEN_FOCUSED).put(keyStroke, "select-all-cast-members");
        component.getInputMap(JComponent.WHEN_ANCESTOR_OF_FOCUSED_COMPONENT).put(keyStroke, "select-all-cast-members");
        component.getActionMap().put("select-all-cast-members", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                action.run();
            }
        });
    }

    private void selectAllFilteredMembers() {
        selectedMemberNums.clear();
        for (CastMemberInfo info : filteredMembers) {
            selectedMemberNums.add(info.memberNum());
        }
        if (!filteredMembers.isEmpty()) {
            context.getSelectionManager().select(SelectionEvent.castMember(0, filteredMembers.get(0).memberNum()));
        }
    }

    private void selectSingleMember(CastMemberInfo info) {
        selectedMemberNums.clear();
        selectedMemberNums.add(info.memberNum());
        context.getSelectionManager().select(SelectionEvent.castMember(0, info.memberNum()));
    }

    private void toggleSelectedMember(CastMemberInfo info) {
        if (!selectedMemberNums.add(info.memberNum())) {
            selectedMemberNums.remove(info.memberNum());
        }
        context.getSelectionManager().select(SelectionEvent.castMember(0, info.memberNum()));
    }

    private void updateGridSelectionVisuals(CastGridPanel grid) {
        for (Component component : grid.getComponents()) {
            if (!(component instanceof JPanel cell)) continue;
            Object memberNumValue = cell.getClientProperty("memberNum");
            boolean selected = memberNumValue instanceof Integer memberNum && selectedMemberNums.contains(memberNum);
            cell.setBorder(BorderFactory.createLineBorder(selected ? new Color(0, 102, 204) : Color.LIGHT_GRAY, selected ? 2 : 1));
            cell.setBackground(selected ? new Color(217, 235, 255) : Color.WHITE);
        }
        grid.repaint();
    }

    private JPanel findGridCellAt(CastGridPanel grid, Point point) {
        Component component = SwingUtilities.getDeepestComponentAt(grid, point.x, point.y);
        while (component != null && component.getParent() != grid) {
            component = component.getParent();
        }
        return component instanceof JPanel panel ? panel : null;
    }

    private void applyListSelection(JList<String> jList) {
        DefaultListSelectionModel selectionModel = new DefaultListSelectionModel();
        for (int i = 0; i < filteredMembers.size(); i++) {
            if (selectedMemberNums.contains(filteredMembers.get(i).memberNum())) {
                selectionModel.addSelectionInterval(i, i);
            }
        }
        jList.setSelectionModel(selectionModel);
    }

    private void syncSelectionFromList(JList<String> jList) {
        selectedMemberNums.clear();
        for (int idx : jList.getSelectedIndices()) {
            if (idx >= 0 && idx < filteredMembers.size()) {
                selectedMemberNums.add(filteredMembers.get(idx).memberNum());
            }
        }
        int leadIdx = jList.getLeadSelectionIndex();
        if (leadIdx >= 0 && leadIdx < filteredMembers.size()) {
            context.getSelectionManager().select(
                SelectionEvent.castMember(0, filteredMembers.get(leadIdx).memberNum()));
        }
    }

    private record ThumbnailResult(int memberNum, BufferedImage thumbnail) {}
}
