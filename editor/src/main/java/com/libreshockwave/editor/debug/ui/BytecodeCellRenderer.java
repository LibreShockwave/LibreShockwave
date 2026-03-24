package com.libreshockwave.editor.debug.ui;

import com.libreshockwave.player.debug.Breakpoint;
import com.libreshockwave.vm.util.StringUtils;

import javax.swing.*;
import java.awt.*;

/**
 * Custom cell renderer for the bytecode list in the debugger.
 * Handles breakpoint markers, current line highlighting, and call instruction links.
 */
public class BytecodeCellRenderer extends DefaultListCellRenderer {

    @Override
    public Component getListCellRendererComponent(JList<?> list, Object value, int index,
                                                  boolean isSelected, boolean cellHasFocus) {
        JLabel label = (JLabel) super.getListCellRendererComponent(list, value, index, isSelected, cellHasFocus);

        if (value instanceof InstructionDisplayItem item) {
            // Build display text with markers using HTML for rich formatting
            StringBuilder sb = new StringBuilder("<html><pre style='margin:0;font-family:monospaced;'>");

            if (item.isLingoLine()) {
                // Lingo line rendering
                // Breakpoint marker (only for lines with a bytecode offset)
                if (item.getOffset() >= 0) {
                    sb.append(getBreakpointMarker(item));
                } else {
                    sb.append("  ");
                }

                // Current instruction marker
                if (item.isCurrent()) {
                    sb.append("<font color='#DAA520'>\u25B6</font> ");
                } else {
                    sb.append("  ");
                }

                // Lingo source text (stored in annotation field)
                String text = item.getAnnotation();
                if (text != null) {
                    sb.append(StringUtils.escapeHtml(text));
                }
            } else {
                // Bytecode line rendering (original)
                sb.append(getBreakpointMarker(item));

                if (item.isCurrent()) {
                    sb.append("<font color='#DAA520'>\u25B6</font> ");
                } else {
                    sb.append("  ");
                }

                sb.append(String.format("[%3d] %-14s", item.getOffset(), item.getOpcode()));
                if (item.getArgument() != 0) {
                    sb.append(String.format(" %-4d", item.getArgument()));
                } else {
                    sb.append("     ");
                }

                String annotation = item.getAnnotation();
                if (annotation != null && !annotation.isEmpty()) {
                    sb.append(" ");
                    if (item.isNavigableCall()) {
                        sb.append("<font color='blue'><u>").append(StringUtils.escapeHtml(annotation)).append("</u></font>");
                    } else {
                        sb.append(StringUtils.escapeHtml(annotation));
                    }
                }
            }

            sb.append("</pre></html>");
            label.setText(sb.toString());

            // Highlighting for current instruction
            if (item.isCurrent() && !isSelected) {
                label.setBackground(new Color(255, 255, 200));  // Light yellow
                label.setOpaque(true);
            }
        }

        return label;
    }

    /**
     * Get the appropriate breakpoint marker based on breakpoint state.
     */
    private String getBreakpointMarker(InstructionDisplayItem item) {
        if (!item.hasBreakpoint() || item.getBreakpoint() == null) {
            if (item.hasBreakpoint()) {
                // Fallback for old-style breakpoints without full info
                return "<font color='red'>\u25CF</font> ";
            }
            return "  ";
        }

        Breakpoint bp = item.getBreakpoint();

        // Disabled breakpoint - gray hollow circle
        if (!bp.enabled()) {
            return "<font color='gray'>\u25CB</font> ";
        }

        // Normal enabled breakpoint - red filled circle
        return "<font color='red'>\u25CF</font> ";
    }
}
