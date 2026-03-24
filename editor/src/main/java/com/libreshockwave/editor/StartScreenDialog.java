package com.libreshockwave.editor;

import javax.swing.*;
import javax.swing.border.EmptyBorder;
import javax.swing.filechooser.FileNameExtensionFilter;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Start screen shown when the editor launches without a movie argument.
 * Displays recent projects, an option to open a movie, and a disabled
 * option to create a new movie — matching the original Director workflow.
 */
public class StartScreenDialog extends JDialog {

    private Path selectedPath;

    public StartScreenDialog(JFrame parent) {
        super(parent, "LibreShockwave Editor", true);
        setDefaultCloseOperation(DISPOSE_ON_CLOSE);
        setResizable(false);
        buildUI();
        pack();
        setLocationRelativeTo(parent);
    }

    private void buildUI() {
        JPanel root = new JPanel(new BorderLayout(0, 12));
        root.setBorder(new EmptyBorder(16, 20, 16, 20));

        // Header
        JLabel title = new JLabel("LibreShockwave Editor");
        title.setFont(title.getFont().deriveFont(Font.BOLD, 18f));
        title.setHorizontalAlignment(SwingConstants.CENTER);

        JLabel subtitle = new JLabel("Director MX 2004");
        subtitle.setFont(subtitle.getFont().deriveFont(Font.PLAIN, 12f));
        subtitle.setForeground(Color.GRAY);
        subtitle.setHorizontalAlignment(SwingConstants.CENTER);

        JPanel header = new JPanel();
        header.setLayout(new BoxLayout(header, BoxLayout.Y_AXIS));
        title.setAlignmentX(Component.CENTER_ALIGNMENT);
        subtitle.setAlignmentX(Component.CENTER_ALIGNMENT);
        header.add(title);
        header.add(Box.createVerticalStrut(2));
        header.add(subtitle);

        root.add(header, BorderLayout.NORTH);

        // Center: recent projects or empty state
        List<String> recent = Preferences.get().getRecentProjects();

        JPanel center = new JPanel(new BorderLayout(0, 6));

        if (!recent.isEmpty()) {
            JLabel recentLabel = new JLabel("Recent Projects:");
            recentLabel.setFont(recentLabel.getFont().deriveFont(Font.BOLD, 12f));
            center.add(recentLabel, BorderLayout.NORTH);

            DefaultListModel<String> listModel = new DefaultListModel<>();
            for (String path : recent) {
                listModel.addElement(path);
            }

            JList<String> recentList = new JList<>(listModel);
            recentList.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
            recentList.setCellRenderer(new RecentProjectRenderer());
            recentList.setVisibleRowCount(Math.min(recent.size(), 8));

            recentList.addMouseListener(new MouseAdapter() {
                @Override
                public void mouseClicked(MouseEvent e) {
                    if (e.getClickCount() == 2) {
                        int idx = recentList.locationToIndex(e.getPoint());
                        if (idx >= 0) {
                            String path = listModel.get(idx);
                            if (Files.exists(Path.of(path))) {
                                selectedPath = Path.of(path);
                                dispose();
                            } else {
                                JOptionPane.showMessageDialog(StartScreenDialog.this,
                                    "File not found:\n" + path,
                                    "File Not Found", JOptionPane.WARNING_MESSAGE);
                            }
                        }
                    }
                }
            });

            // Enter key opens selected project
            recentList.addKeyListener(new KeyAdapter() {
                @Override
                public void keyPressed(KeyEvent e) {
                    if (e.getKeyCode() == KeyEvent.VK_ENTER) {
                        int idx = recentList.getSelectedIndex();
                        if (idx >= 0) {
                            String path = listModel.get(idx);
                            if (Files.exists(Path.of(path))) {
                                selectedPath = Path.of(path);
                                dispose();
                            }
                        }
                    }
                }
            });

            JScrollPane scrollPane = new JScrollPane(recentList);
            scrollPane.setPreferredSize(new Dimension(450, 200));
            center.add(scrollPane, BorderLayout.CENTER);
        } else {
            JLabel emptyLabel = new JLabel("No recent projects. Open a movie to get started.");
            emptyLabel.setForeground(Color.GRAY);
            emptyLabel.setHorizontalAlignment(SwingConstants.CENTER);
            emptyLabel.setPreferredSize(new Dimension(450, 60));
            center.add(emptyLabel, BorderLayout.CENTER);
        }

        root.add(center, BorderLayout.CENTER);

        // Bottom buttons
        JPanel buttons = new JPanel(new FlowLayout(FlowLayout.CENTER, 10, 0));

        JButton newMovie = new JButton("Create New Movie");
        newMovie.setEnabled(false);
        newMovie.setToolTipText("Not yet available");
        buttons.add(newMovie);

        JButton openMovie = new JButton("Open Movie...");
        openMovie.addActionListener(e -> openFileChooser());
        buttons.add(openMovie);

        root.add(buttons, BorderLayout.SOUTH);

        setContentPane(root);

        // Escape closes
        getRootPane().registerKeyboardAction(
            e -> dispose(),
            KeyStroke.getKeyStroke(KeyEvent.VK_ESCAPE, 0),
            JComponent.WHEN_IN_FOCUSED_WINDOW
        );
    }

    private void openFileChooser() {
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle("Open Director File");
        chooser.setFileFilter(new FileNameExtensionFilter(
            "Director Files (*.dir, *.dxr, *.dcr, *.cct, *.cst)",
            "dir", "dxr", "dcr", "cct", "cst"
        ));

        String lastDir = Preferences.get().getLastOpenDirectory();
        if (lastDir != null) {
            File dir = new File(lastDir);
            if (dir.isDirectory()) {
                chooser.setCurrentDirectory(dir);
            }
        }

        if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            File selected = chooser.getSelectedFile();
            Preferences.get().setLastOpenDirectory(selected.getParent());
            selectedPath = selected.toPath();
            dispose();
        }
    }

    /**
     * Show the dialog and return the selected path, or null if cancelled.
     */
    public Path showDialog() {
        setVisible(true);
        return selectedPath;
    }

    /**
     * Renders recent project entries showing filename and parent directory.
     */
    private static class RecentProjectRenderer extends DefaultListCellRenderer {
        @Override
        public Component getListCellRendererComponent(JList<?> list, Object value,
                int index, boolean isSelected, boolean cellHasFocus) {
            super.getListCellRendererComponent(list, value, index, isSelected, cellHasFocus);
            String path = (String) value;
            Path p = Path.of(path);
            String filename = p.getFileName().toString();
            String parent = p.getParent() != null ? p.getParent().toString() : "";

            setText("<html><b>" + escapeHtml(filename) + "</b> &mdash; <font color='gray'>"
                + escapeHtml(parent) + "</font></html>");

            if (!Files.exists(p)) {
                setText("<html><b><font color='gray'><s>" + escapeHtml(filename)
                    + "</s></font></b> &mdash; <font color='gray'>"
                    + escapeHtml(parent) + " (missing)</font></html>");
            }

            setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));
            return this;
        }

        private static String escapeHtml(String s) {
            return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
        }
    }
}
