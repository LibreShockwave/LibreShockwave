package com.libreshockwave.editor.cast;

import com.libreshockwave.cast.MemberType;
import com.libreshockwave.editor.model.CastMemberInfo;

import javax.swing.*;
import java.awt.*;

/**
 * Grid view of cast members with thumbnails.
 * Displays members as a grid of cells with type icons and names.
 * Implements Scrollable so cells wrap into rows that fit the viewport width,
 * with vertical scrolling only.
 */
public class CastGridPanel extends JPanel implements Scrollable {

    private static final int CELL_WIDTH = 72;
    private static final int CELL_HEIGHT = 80;
    private static final int GAP = 4;
    private static final int THUMB_SIZE = 48;

    public CastGridPanel() {
        setLayout(new WrapLayout());
        setBackground(Color.WHITE);
        setFocusable(true);
    }

    /**
     * A layout manager that arranges components in a left-aligned, wrapping grid.
     * Unlike FlowLayout, it reports a preferred height that accounts for wrapping
     * at the current panel width, so JScrollPane can scroll vertically.
     */
    private static class WrapLayout implements LayoutManager {
        @Override
        public void addLayoutComponent(String name, Component comp) {}

        @Override
        public void removeLayoutComponent(Component comp) {}

        @Override
        public Dimension preferredLayoutSize(Container parent) {
            return computeSize(parent);
        }

        @Override
        public Dimension minimumLayoutSize(Container parent) {
            return computeSize(parent);
        }

        @Override
        public void layoutContainer(Container parent) {
            Insets insets = parent.getInsets();
            int maxWidth = parent.getWidth() - insets.left - insets.right;
            if (maxWidth <= 0) maxWidth = Integer.MAX_VALUE;

            int x = insets.left + GAP;
            int y = insets.top + GAP;
            int rowHeight = 0;

            for (Component comp : parent.getComponents()) {
                if (!comp.isVisible()) continue;
                Dimension d = comp.getPreferredSize();

                if (x + d.width + GAP > maxWidth + insets.left && x > insets.left + GAP) {
                    // Wrap to next row
                    x = insets.left + GAP;
                    y += rowHeight + GAP;
                    rowHeight = 0;
                }

                comp.setBounds(x, y, d.width, d.height);
                x += d.width + GAP;
                rowHeight = Math.max(rowHeight, d.height);
            }
        }

        private Dimension computeSize(Container parent) {
            Insets insets = parent.getInsets();
            int maxWidth = parent.getWidth();
            if (maxWidth <= 0) {
                // Before first layout, use parent's parent (viewport) width if available
                if (parent.getParent() != null) {
                    maxWidth = parent.getParent().getWidth();
                }
            }
            if (maxWidth <= 0) maxWidth = 400; // fallback default

            int availableWidth = maxWidth - insets.left - insets.right;

            int x = GAP;
            int y = GAP;
            int rowHeight = 0;

            for (Component comp : parent.getComponents()) {
                if (!comp.isVisible()) continue;
                Dimension d = comp.getPreferredSize();

                if (x + d.width + GAP > availableWidth && x > GAP) {
                    x = GAP;
                    y += rowHeight + GAP;
                    rowHeight = 0;
                }

                x += d.width + GAP;
                rowHeight = Math.max(rowHeight, d.height);
            }

            int totalHeight = y + rowHeight + GAP + insets.top + insets.bottom;
            return new Dimension(maxWidth, totalHeight);
        }
    }

    /**
     * Add a cell for a cast member. Returns the thumbnail JLabel so callers
     * can replace the icon later (e.g. with a decoded bitmap thumbnail).
     */
    public JLabel addMemberCell(CastMemberInfo info) {
        JPanel cell = new JPanel(new BorderLayout());
        cell.setPreferredSize(new Dimension(CELL_WIDTH, CELL_HEIGHT));
        cell.setBorder(BorderFactory.createLineBorder(Color.LIGHT_GRAY));
        cell.setBackground(Color.WHITE);
        cell.putClientProperty("memberNum", info.memberNum());
        cell.putClientProperty("memberIndex", getComponentCount());

        // Thumbnail area
        JLabel thumbnail = new JLabel();
        thumbnail.setHorizontalAlignment(SwingConstants.CENTER);
        thumbnail.setPreferredSize(new Dimension(THUMB_SIZE, THUMB_SIZE));
        thumbnail.setOpaque(true);
        thumbnail.setBackground(new Color(240, 240, 240));
        thumbnail.putClientProperty("memberNum", info.memberNum());

        // Create placeholder icon based on type
        String abbrev = getTypeAbbreviation(info.memberType());
        thumbnail.setIcon(new ImageIcon(CastThumbnailRenderer.createPlaceholder(abbrev)));

        // Name label: show name if present, otherwise [type]
        String displayName;
        if (info.name() != null && !info.name().isEmpty()) {
            displayName = info.name();
        } else {
            displayName = "[" + abbrev + "]";
        }
        JLabel nameLabel = new JLabel(displayName, SwingConstants.CENTER);
        nameLabel.setFont(nameLabel.getFont().deriveFont(9f));
        nameLabel.setToolTipText(displayName);
        nameLabel.putClientProperty("memberNum", info.memberNum());

        cell.add(thumbnail, BorderLayout.CENTER);
        cell.add(nameLabel, BorderLayout.SOUTH);

        add(cell);
        return thumbnail;
    }

    private static String getTypeAbbreviation(MemberType type) {
        return switch (type) {
            case BITMAP -> "Bmp";
            case SCRIPT -> "Scr";
            case SOUND -> "Snd";
            case TEXT -> "Txt";
            case RICH_TEXT -> "RTx";
            case BUTTON -> "Btn";
            case SHAPE -> "Shp";
            case FILM_LOOP -> "Flm";
            case PALETTE -> "Pal";
            case TRANSITION -> "Trn";
            case FONT -> "Fnt";
            case DIGITAL_VIDEO -> "Vid";
            case SHOCKWAVE_3D -> "3D";
            case MOVIE -> "Mov";
            case PICTURE -> "Pic";
            case XTRA -> "Xtr";
            default -> "?";
        };
    }

    public void clearMembers() {
        removeAll();
        revalidate();
        repaint();
    }

    // --- Scrollable implementation ---

    @Override
    public Dimension getPreferredScrollableViewportSize() {
        return getPreferredSize();
    }

    @Override
    public int getScrollableUnitIncrement(Rectangle visibleRect, int orientation, int direction) {
        return CELL_HEIGHT + GAP;
    }

    @Override
    public int getScrollableBlockIncrement(Rectangle visibleRect, int orientation, int direction) {
        return orientation == SwingConstants.VERTICAL ? visibleRect.height : visibleRect.width;
    }

    @Override
    public boolean getScrollableTracksViewportWidth() {
        return true;
    }

    @Override
    public boolean getScrollableTracksViewportHeight() {
        return false;
    }
}
