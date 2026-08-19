#pragma once

#include <QRegularExpression>
#include <QSet>
#include <optional>

#include "ui/highlight/GutterHighlighter.hpp"

class QTextDocument;

namespace libreshockwave::debugger {

/// Syntax highlighter for the bytecode view.  Each line follows the fixed
/// layout built by CodeViewPanel:
///   [> ]([▶●]|__)NNNN  opcode        arg  ; annotation
/// The offset, the opcode (colored by category), and the `; annotation`
/// comment get distinct colors; the gutter marker is shared with the Lingo
/// view via GutterHighlighter.  Declaration names inside the annotation get
/// a single underline, marking the right-click "Go to declaration" targets
/// the same way the decompiled view does.
class BytecodeHighlighter : public GutterHighlighter {
    Q_OBJECT

public:
    explicit BytecodeHighlighter(QTextDocument* document);

    /// Replace the movie-wide (lower-cased) declaration names and
    /// re-highlight.  These words receive a single underline when they
    /// appear in the `; annotation` column.
    void setDeclarationNames(const QSet<QString>& names);

    /// Category color for an opcode mnemonic (camelCase, e.g. "pushZero");
    /// std::nullopt leaves the opcode in the default text color.
    static std::optional<QColor> categoryColor(const QString& opcode);
    static QColor offsetColor();
    static QColor commentColor();

private:
    void highlightBlock(const QString& text) override;

    QSet<QString> declarationNames_;
    QRegularExpression lineRegex_;
};

} // namespace libreshockwave::debugger
