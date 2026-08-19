#pragma once

#include <QRegularExpression>
#include <QSet>
#include <optional>

#include "ui/highlight/GutterHighlighter.hpp"

class QTextDocument;

namespace libreshockwave::debugger {

/// Syntax highlighter for decompiled Lingo in the code view.  Colors
/// keywords, builtin words, strings, numbers, `#symbol` values, and known
/// movie method names (handler calls).  Declaration words can be underlined
/// internally to show which words the right-click "Go to declaration" resolves:
/// movie-wide declarations get a single underline, and current-handler
/// arguments and locals get a dotted underline.  The color scheme adapts to the
/// editor's light or dark base palette.
///
/// Usage:
///   LingoHighlighter highlighter{view->document()};
///   highlighter.setMethodNames({"foo", "bar"});       // recolors in place
///   highlighter.setDeclarationNames({"foo", "speed"}); // single underline
///   highlighter.setVariableNames({"lFoo"});           // dotted underline
class LingoHighlighter : public GutterHighlighter {
    Q_OBJECT

public:
    explicit LingoHighlighter(QTextDocument* document);

    /// Replace the set of known (lower-cased) method names and re-highlight.
    /// Methods are colored; declaration underlines are controlled internally.
    void setMethodNames(const QSet<QString>& names);

    /// Replace the movie-wide (lower-cased) declaration names — methods,
    /// properties, and globals — and re-highlight.  These names are also used
    /// by right-click "Go to declaration" even when underlines are disabled.
    void setDeclarationNames(const QSet<QString>& names);

    /// Replace the current handler's (lower-cased) argument and local names
    /// and re-highlight.  These names remain available for right-click
    /// "Go to variable/argument declaration" when underlines are disabled.
    void setVariableNames(const QSet<QString>& names);

    /// Scheme colors, exposed so tests can assert on the produced formats.
    static QColor keywordColor();
    static QColor builtinColor();
    static QColor stringColor();
    static QColor numberColor();
    static QColor symbolColor();
    static QColor methodColor();

    /// Color for a lower-cased identifier: keyword, builtin, or known method;
    /// std::nullopt when the word is plain text.
    static std::optional<QColor> identifierColor(const QString& lowerName,
                                                 const QSet<QString>& methods);

private:
    void highlightBlock(const QString& text) override;

    QSet<QString> methodNames_;
    QSet<QString> declarationNames_; // movie-wide: methods + properties + globals
    QSet<QString> variableNames_;    // current handler: arguments + locals
    QRegularExpression tokenRegex_;
};

} // namespace libreshockwave::debugger
