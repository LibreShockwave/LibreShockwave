#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

class QTextDocument;

namespace libreshockwave::debugger {

/// Base for the code view highlighters.  Owns the light/dark scheme choice
/// and the gutter marker coloring shared by the bytecode and Lingo views:
/// the `> ` current-line prefix, the `▶` current marker, and the `●`
/// breakpoint marker that the code view prefixes to lines.
class GutterHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit GutterHighlighter(QTextDocument* document);

    /// Marker color: the current-line arrow, or red for a breakpoint dot.
    static QColor markerColor(bool breakpoint);

protected:
    /// True when the editor's base palette is dark (color scheme choice).
    static bool darkBase();

    /// Colors the optional `> ` prefix and the leading `▶`/`●` marker.
    void applyGutterMarker(const QString& text);
};

} // namespace libreshockwave::debugger
