#include "EditorStyle.hpp"

#include <libpanel.h>

void loadEditorCss() {
    const char* css = R"CSS(
window {
  background: #242424;
  color: #eeeeee;
}
.welcome-page {
  background: #1f1f1f;
}
.welcome-title {
  font-size: 28px;
  font-weight: 700;
}
.welcome-subtitle {
  color: #b8b8b8;
}
.section-title,
.toolbar-title,
.window-title {
  font-weight: 700;
}
.toolbar-subtitle,
.window-subtitle,
.asset-subtitle,
.status-label,
.frame-label {
  color: #b8b8b8;
  font-size: 12px;
}
.window-controls {
  border-spacing: 2px;
}
.window-controls button {
  min-width: 32px;
  min-height: 28px;
  padding: 0;
}
.close-window-button:hover {
  background: #9f2d2d;
}
.menu-strip {
  padding: 2px 8px;
  background: #202020;
  border-bottom: 1px solid #3a3a3a;
}
.menu-strip-button {
  padding: 3px 8px;
}
.studio-toolbar {
  padding: 6px 10px;
  background: #303030;
  border-bottom: 1px solid #3c3c3c;
}
.status-strip {
  padding: 4px 8px;
  background: #202020;
  border-top: 1px solid #393939;
}
.stage-view,
.asset-preview {
  background: #141414;
}
.code-preview {
  background: #161616;
  color: #eeeeee;
  font-size: 12px;
}
.boxed-list,
list {
  background: #2a2a2a;
}
row {
  border-bottom: 1px solid #383838;
}
row:selected {
  background: #34548a;
}
.asset-row {
  min-height: 42px;
}
.cast-preview-row {
  min-height: 58px;
}
.cast-preview,
.cast-preview-icon {
  background: #151515;
  border: 1px solid #3a3a3a;
  border-radius: 4px;
}
.asset-title {
  font-weight: 600;
}
.error {
  color: #ff9a9a;
}
.property-row {
  min-height: 30px;
}
.property-key {
  color: #bbbbbb;
  min-width: 120px;
}
.property-value {
  color: #f2f2f2;
}
)CSS";

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
