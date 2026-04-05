package com.libreshockwave.editor.property;

/**
 * Sprite properties tab for the Property Inspector.
 * Displays locH, locV, width, height, ink, blend, etc.
 */
public class SpriteTab extends PropertyGridTab {

    public SpriteTab() {
        super(
                "Sprite:",
                "Member:",
                "X (locH):",
                "Y (locV):",
                "Width:",
                "Height:",
                "Ink:",
                "Blend:",
                "locZ:",
                "Visible:",
                "Moveable:",
                "Editable:");
    }
}
