package com.libreshockwave.editor.property;

import javax.swing.*;
import java.awt.*;

/**
 * Shared grid-based read-only property tab.
 */
abstract class PropertyGridTab extends JPanel {

    protected PropertyGridTab(String... propertyLabels) {
        setLayout(new GridBagLayout());
        GridBagConstraints gbc = new GridBagConstraints();
        gbc.insets = new Insets(2, 4, 2, 4);
        gbc.fill = GridBagConstraints.HORIZONTAL;
        gbc.anchor = GridBagConstraints.WEST;

        int row = 0;
        for (String label : propertyLabels) {
            addProperty(gbc, row++, label, "-");
        }

        gbc.gridy = row;
        gbc.weighty = 1.0;
        add(new JPanel(), gbc);
    }

    private void addProperty(GridBagConstraints gbc, int row, String label, String value) {
        gbc.gridy = row;
        gbc.gridx = 0;
        gbc.weightx = 0;
        add(new JLabel(label), gbc);

        gbc.gridx = 1;
        gbc.weightx = 1.0;
        JTextField field = new JTextField(value);
        field.setEditable(false);
        add(field, gbc);
    }
}
