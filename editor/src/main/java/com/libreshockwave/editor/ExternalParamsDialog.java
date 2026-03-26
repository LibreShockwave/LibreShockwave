package com.libreshockwave.editor;

import javax.swing.*;
import javax.swing.table.AbstractTableModel;
import java.awt.*;
import java.awt.event.KeyEvent;
import java.util.*;
import java.util.List;

/**
 * Dialog for editing Shockwave external parameters (PARAM tags).
 * Allows users to add/remove key-value pairs that are accessible
 * to Lingo scripts via externalParamValue().
 */
public class ExternalParamsDialog extends JDialog {

    private final ParamsTableModel tableModel;
    private final JTable table;
    private boolean confirmed = false;

    public ExternalParamsDialog(JFrame parent, Map<String, String> currentParams) {
        super(parent, "External Parameters", true);

        tableModel = new ParamsTableModel(currentParams);
        table = new JTable(tableModel);
        table.setRowHeight(24);
        table.getColumnModel().getColumn(0).setPreferredWidth(120);
        table.getColumnModel().getColumn(1).setPreferredWidth(400);

        // Stop editing when focus leaves the table (prevents lost edits)
        table.putClientProperty("terminateEditOnFocusLost", Boolean.TRUE);

        JScrollPane scrollPane = new JScrollPane(table);
        scrollPane.setPreferredSize(new Dimension(560, 200));

        // Buttons panel
        JPanel buttonPanel = new JPanel(new FlowLayout(FlowLayout.LEFT));

        JButton addButton = new JButton("Add");
        addButton.setMnemonic(KeyEvent.VK_A);
        addButton.addActionListener(e -> {
            stopEditing();
            tableModel.addRow("", "");
            int row = tableModel.getRowCount() - 1;
            table.setRowSelectionInterval(row, row);
            table.scrollRectToVisible(table.getCellRect(row, 0, true));
            table.editCellAt(row, 0);
            table.getEditorComponent().requestFocusInWindow();
        });
        buttonPanel.add(addButton);

        JButton removeButton = new JButton("Remove");
        removeButton.setMnemonic(KeyEvent.VK_R);
        removeButton.addActionListener(e -> {
            stopEditing();
            int[] rows = table.getSelectedRows();
            if (rows.length > 0) {
                // Remove from bottom to top to preserve indices
                Arrays.sort(rows);
                for (int i = rows.length - 1; i >= 0; i--) {
                    tableModel.removeRow(rows[i]);
                }
            }
        });
        buttonPanel.add(removeButton);

        buttonPanel.add(Box.createHorizontalStrut(20));

        JButton habboPresetButton = new JButton("Habbo Preset");
        habboPresetButton.setToolTipText("Load default Habbo external parameters");
        habboPresetButton.addActionListener(e -> {
            stopEditing();
            loadHabboPreset();
        });
        buttonPanel.add(habboPresetButton);

        // OK/Cancel panel
        JPanel okCancelPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT));

        JButton okButton = new JButton("OK");
        okButton.addActionListener(e -> {
            stopEditing();
            confirmed = true;
            dispose();
        });
        okCancelPanel.add(okButton);

        JButton cancelButton = new JButton("Cancel");
        cancelButton.addActionListener(e -> dispose());
        okCancelPanel.add(cancelButton);

        // Layout
        JPanel bottomPanel = new JPanel(new BorderLayout());
        bottomPanel.add(buttonPanel, BorderLayout.WEST);
        bottomPanel.add(okCancelPanel, BorderLayout.EAST);

        JLabel helpLabel = new JLabel(
            "  Set key-value pairs accessible via externalParamValue() in Lingo scripts.");
        helpLabel.setFont(helpLabel.getFont().deriveFont(Font.PLAIN, 11f));
        helpLabel.setBorder(BorderFactory.createEmptyBorder(4, 0, 4, 0));

        setLayout(new BorderLayout(8, 8));
        add(helpLabel, BorderLayout.NORTH);
        add(scrollPane, BorderLayout.CENTER);
        add(bottomPanel, BorderLayout.SOUTH);

        getRootPane().setDefaultButton(okButton);

        // Escape closes dialog
        getRootPane().registerKeyboardAction(
            e -> dispose(),
            KeyStroke.getKeyStroke(KeyEvent.VK_ESCAPE, 0),
            JComponent.WHEN_IN_FOCUSED_WINDOW
        );

        pack();
        setLocationRelativeTo(parent);
    }

    private void stopEditing() {
        if (table.isEditing()) {
            table.getCellEditor().stopCellEditing();
        }
    }

    private void loadHabboPreset() {
        tableModel.clear();
        // Standard Habbo external parameters
        // sw1-sw9 are the Shockwave embed params that contain semicolon-separated key=value pairs
        tableModel.addRow("sw1",
            "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
        tableModel.addRow("sw2",
            "connection.info.host=au.h4bbo.net;connection.info.port=30101");
        tableModel.addRow("sw3",
            "client.reload.url=http://h4bbo.net/");
        tableModel.addRow("sw4",
            "connection.mus.host=au.h4bbo.net;connection.mus.port=38101");
        tableModel.addRow("sw5",
            "external.variables.txt=http://sandbox.h4bbo.net/gamedata/external_variables.txt;" +
            "external.texts.txt=http://sandbox.h4bbo.net/gamedata/external_texts.txt");
        tableModel.fireTableDataChanged();
    }

    /**
     * Show the dialog and return the result.
     * @return The edited params map, or null if cancelled
     */
    public Map<String, String> showDialog() {
        setVisible(true);
        if (confirmed) {
            return tableModel.toMap();
        }
        return null;
    }

    /**
     * Table model for key-value parameter pairs.
     */
    private static class ParamsTableModel extends AbstractTableModel {
        private final List<String[]> rows = new ArrayList<>();

        ParamsTableModel(Map<String, String> params) {
            if (params != null) {
                for (var entry : params.entrySet()) {
                    rows.add(new String[]{entry.getKey(), entry.getValue()});
                }
            }
        }

        void addRow(String key, String value) {
            rows.add(new String[]{key, value});
            fireTableRowsInserted(rows.size() - 1, rows.size() - 1);
        }

        void removeRow(int index) {
            if (index >= 0 && index < rows.size()) {
                rows.remove(index);
                fireTableRowsDeleted(index, index);
            }
        }

        void clear() {
            int size = rows.size();
            if (size > 0) {
                rows.clear();
                fireTableRowsDeleted(0, size - 1);
            }
        }

        Map<String, String> toMap() {
            Map<String, String> map = new LinkedHashMap<>();
            for (String[] row : rows) {
                String key = row[0].trim();
                if (!key.isEmpty()) {
                    map.put(key, row[1]);
                }
            }
            return map;
        }

        @Override
        public int getRowCount() {
            return rows.size();
        }

        @Override
        public int getColumnCount() {
            return 2;
        }

        @Override
        public String getColumnName(int column) {
            return column == 0 ? "Key" : "Value";
        }

        @Override
        public Object getValueAt(int row, int column) {
            return rows.get(row)[column];
        }

        @Override
        public void setValueAt(Object value, int row, int column) {
            rows.get(row)[column] = value.toString();
            fireTableCellUpdated(row, column);
        }

        @Override
        public boolean isCellEditable(int row, int column) {
            return true;
        }
    }
}
