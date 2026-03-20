package com.libreshockwave.editor.cast;

import javax.swing.*;
import java.awt.*;
import java.awt.event.MouseEvent;
import java.awt.event.MouseListener;

/**
 * List view of cast members as an alternative to the grid view.
 */
public class CastListPanel extends JPanel {

    private final DefaultListModel<String> listModel;
    private final JList<String> list;

    public CastListPanel() {
        setLayout(new java.awt.BorderLayout());

        listModel = new DefaultListModel<>();
        list = new PopupFriendlyList(listModel);
        list.setFont(list.getFont().deriveFont(12f));

        add(new JScrollPane(list), java.awt.BorderLayout.CENTER);
    }

    public void addMember(int memberNum, String name, String type) {
        String display = memberNum + ": " + (name != null ? name : "(unnamed)") + " [" + type + "]";
        listModel.addElement(display);
    }

    public JList<String> getList() {
        return list;
    }

    public void clearMembers() {
        listModel.clear();
    }

    /**
     * Prevent Swing's default list UI from collapsing an existing multi-selection
     * when the user right-clicks one of the already-selected rows.
     */
    private static class PopupFriendlyList extends JList<String> {
        PopupFriendlyList(DefaultListModel<String> model) {
            super(model);
        }

        @Override
        protected void processMouseEvent(MouseEvent e) {
            if (shouldPreserveSelection(e)) {
                dispatchToCustomListeners(e);
                return;
            }
            super.processMouseEvent(e);
        }

        private boolean shouldPreserveSelection(MouseEvent e) {
            if (!SwingUtilities.isRightMouseButton(e) && !e.isPopupTrigger()) {
                return false;
            }

            int index = locationToIndex(e.getPoint());
            Rectangle cellBounds = index >= 0 ? getCellBounds(index, index) : null;
            return cellBounds != null
                && cellBounds.contains(e.getPoint())
                && isSelectedIndex(index)
                && getSelectedIndices().length > 1;
        }

        private void dispatchToCustomListeners(MouseEvent e) {
            for (MouseListener listener : getMouseListeners()) {
                String listenerName = listener.getClass().getName();
                if (listenerName.startsWith("javax.swing.plaf.basic.BasicListUI")) {
                    continue;
                }

                switch (e.getID()) {
                    case MouseEvent.MOUSE_PRESSED -> listener.mousePressed(e);
                    case MouseEvent.MOUSE_RELEASED -> listener.mouseReleased(e);
                    case MouseEvent.MOUSE_CLICKED -> listener.mouseClicked(e);
                    case MouseEvent.MOUSE_ENTERED -> listener.mouseEntered(e);
                    case MouseEvent.MOUSE_EXITED -> listener.mouseExited(e);
                    default -> {
                    }
                }
            }
        }
    }
}
