#pragma once

#include <QRegularExpression>
#include <QSet>
#include <optional>

#include "ui/highlight/GutterHighlighter.hpp"

class QTextDocument;

namespace libreshockwave::debugger {

/// Syntax highlighter for decompiled Lingo in the code view.  Colors
/// keywords, builtin words, strings, numbers, `#symbol` values, and known
/// movie method names (handler calls).  The color scheme adapts to the
/// editor's light or dark base palette.
///
/// Usage:
///   LingoHighlighter highlighter{view->document()};
///   highlighter.setMethodNames({"foo", "bar"}); // recolors in place
class LingoHighlighter : public GutterHighlighter {
    Q_OBJECT

public:
    explicit LingoHighlighter(QTextDocument* document);

    /// Replace the set of known (lower-cased) method names and re-highlight.
    void setMethodNames(const QSet<QString>& names);

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
    QRegularExpression tokenRegex_;
};

} // namespace libreshockwave::debugger
