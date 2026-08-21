#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

class QTextDocument;

namespace libreshockwave::debugger {

/// Base for the code view syntax highlighters.  Owns the light/dark color
/// scheme choice shared by the bytecode and Lingo views.  The per-line current
/// and breakpoint indicators live in the gutter widget (`CodeGutter`), not in
/// the highlighted text, so this class no longer colors any gutter markers.
class GutterHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit GutterHighlighter(QTextDocument* document);

protected:
    /// True when the editor's base palette is dark (color scheme choice).
    static bool darkBase();
};

} // namespace libreshockwave::debugger
