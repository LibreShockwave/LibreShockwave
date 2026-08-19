#include "GutterHighlighter.hpp"

#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QTextCharFormat>

namespace libreshockwave::debugger {

namespace {

// Display-asset colors (VS Code light/dark theme inspired).
constexpr int kLightBaseThreshold = 128;
const QColor kLightMarkerCurrent(0xd9, 0x77, 0x06);
const QColor kLightMarkerBreakpoint(0xdc, 0x26, 0x26);
const QColor kDarkMarkerCurrent(0xfb, 0xbf, 0x24);
const QColor kDarkMarkerBreakpoint(0xf8, 0x71, 0x71);
const QChar kCurrentLineMarker{0x25B6}; // ▶
const QChar kBreakpointMarker{0x25CF};  // ●

} // namespace

GutterHighlighter::GutterHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
}

bool GutterHighlighter::darkBase() {
    const int baseGray =
        qGray(QGuiApplication::palette().color(QPalette::Base).rgba());
    return baseGray < kLightBaseThreshold;
}

QColor GutterHighlighter::markerColor(bool breakpoint) {
    if (darkBase()) {
        return breakpoint ? kDarkMarkerBreakpoint : kDarkMarkerCurrent;
    }
    return breakpoint ? kLightMarkerBreakpoint : kLightMarkerCurrent;
}

void GutterHighlighter::applyGutterMarker(const QString& text) {
    int pos = 0;
    if (text.startsWith(QLatin1String("> "))) {
        QTextCharFormat prefix;
        prefix.setForeground(markerColor(false));
        prefix.setFontWeight(QFont::Bold);
        setFormat(0, 2, prefix);
        pos = 2;
    }
    if (pos < text.size()) {
        const QChar c = text.at(pos);
        if (c == kCurrentLineMarker || c == kBreakpointMarker) {
            QTextCharFormat marker;
            marker.setForeground(markerColor(c == kBreakpointMarker));
            setFormat(pos, 1, marker);
        }
    }
}

} // namespace libreshockwave::debugger
