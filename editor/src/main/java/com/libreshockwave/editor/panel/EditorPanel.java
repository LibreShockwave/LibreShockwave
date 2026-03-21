package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.selection.SelectionEvent;
import com.libreshockwave.editor.selection.SelectionListener;

import javax.swing.*;
import java.beans.PropertyChangeEvent;

/**
 * Abstract base class for all editor panels.
 * Extends JInternalFrame for MDI support and listens to EditorContext events.
 */
public abstract class EditorPanel extends JInternalFrame implements SelectionListener {

    private final String panelId;
    protected final EditorContext context;

    protected EditorPanel(String panelId, String title, EditorContext context,
                          boolean resizable, boolean closable,
                          boolean maximizable, boolean iconifiable) {
        super(title, resizable, closable, maximizable, iconifiable);
        this.panelId = panelId;
        this.context = context;

        // Listen to context property changes
        context.addPropertyChangeListener(this::onPropertyChange);
        context.getSelectionManager().addListener(this);
    }

    private void onPropertyChange(PropertyChangeEvent evt) {
        switch (evt.getPropertyName()) {
            case EditorContext.PROP_FILE -> {
                DirectorFile newFile = (DirectorFile) evt.getNewValue();
                if (newFile != null) {
                    onFileOpened(newFile);
                } else {
                    onFileClosed();
                }
            }
            case EditorContext.PROP_FRAME -> {
                int frame = (int) evt.getNewValue();
                onFrameChanged(frame);
            }
            case EditorContext.PROP_CASTS_LOADED -> onCastsLoaded();
        }
    }

    /** Stable identifier for this panel, used as map key and for layout persistence. */
    public String getPanelId() {
        return panelId;
    }

    /**
     * Called when a Director file is opened.
     */
    protected void onFileOpened(DirectorFile file) {
        // Override in subclasses
    }

    /**
     * Called when the current file is closed.
     */
    protected void onFileClosed() {
        // Override in subclasses
    }

    /**
     * Called when the current frame changes.
     */
    protected void onFrameChanged(int frame) {
        // Override in subclasses
    }

    /**
     * Called when external casts finish loading.
     */
    protected void onCastsLoaded() {
        // Override in subclasses
    }

    @Override
    public void selectionChanged(SelectionEvent event) {
        onSelectionChanged(event);
    }

    /**
     * Called when the selection changes.
     */
    protected void onSelectionChanged(SelectionEvent event) {
        // Override in subclasses
    }
}
