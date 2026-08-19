#pragma once

#include <QRegularExpression>
#include <QSet>
#include <optional>

#include "ui/highlight/GutterHighlighter.hpp"

class QTextDocument;

namespace libreshockwave::debugger {

/// Syntax highlighter for decompiled Lingo in the code view.  Colors
/// keywords, builtin words, strings, numbers, `#symbol` values, and known
/// movie method names (handler calls).  Declaration words are additionally
/// underlined so the reader sees which words the right-click "Go to
/// declaration" resolves: movie-wide declarations (methods, properties,
/// globals) get a single underline, and the current handler's arguments and
/// locals get a dotted underline.  The color scheme adapts to the editor's
/// light or dark base palette.
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
    /// Methods are colored and underlined.
    void setMethodNames(const QSet<QString>& names);

    /// Replace the movie-wide (lower-cased) declaration names — methods,
    /// properties, and globals — and re-highlight.  These words receive a
    /// single underline marking the right-click "Go to declaration" targets.
    void setDeclarationNames(const QSet<QString>& names);

    /// Replace the current handler's (lower-cased) argument and local names
    /// and re-highlight.  These words receive a dotted underline marking the
    /// right-click "Go to variable/argument declaration" targets.
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
