#include "GutterHighlighter.hpp"

#include <QGuiApplication>
#include <QPalette>

namespace libreshockwave::debugger {

namespace {

// Threshold on the editor base-palette luminance for the light/dark choice.
constexpr int kLightBaseThreshold = 128;

} // namespace

GutterHighlighter::GutterHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
}

bool GutterHighlighter::darkBase() {
    const int baseGray =
        qGray(QGuiApplication::palette().color(QPalette::Base).rgba());
    return baseGray < kLightBaseThreshold;
}

} // namespace libreshockwave::debugger
