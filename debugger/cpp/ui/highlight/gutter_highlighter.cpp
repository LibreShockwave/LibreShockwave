#include "gutter_highlighter.hpp"

#include <QGuiApplication>
#include <QPalette>

namespace libreshockwave::debugger {

namespace {

// Threshold on the editor base-palette luminance for the light/dark choice.
constexpr int k_light_base_threshold = 128;

} // namespace

GutterHighlighter::GutterHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
}

bool GutterHighlighter::darkBase() {
    const int base_gray =
        qGray(QGuiApplication::palette().color(QPalette::Base).rgba());
    return base_gray < k_light_base_threshold;
}

} // namespace libreshockwave::debugger
