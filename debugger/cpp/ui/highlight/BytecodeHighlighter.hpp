#pragma once

#include <QRegularExpression>
#include <optional>

#include "ui/highlight/GutterHighlighter.hpp"

class QTextDocument;

namespace libreshockwave::debugger {

/// Syntax highlighter for the bytecode view.  Each line follows the fixed
/// layout built by CodeViewPanel:
///   [> ]([▶●]|__)NNNN  opcode        arg  ; annotation
/// The offset, the opcode (colored by category), and the `; annotation`
/// comment get distinct colors; the gutter marker is shared with the Lingo
/// view via GutterHighlighter.
class BytecodeHighlighter : public GutterHighlighter {
    Q_OBJECT

public:
    explicit BytecodeHighlighter(QTextDocument* document);

    /// Category color for an opcode mnemonic (camelCase, e.g. "pushZero");
    /// std::nullopt leaves the opcode in the default text color.
    static std::optional<QColor> categoryColor(const QString& opcode);
    static QColor offsetColor();
    static QColor commentColor();

private:
    void highlightBlock(const QString& text) override;

    QRegularExpression lineRegex_;
};

} // namespace libreshockwave::debugger
