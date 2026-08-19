#include <QFont>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>

#include "ui/highlight/BytecodeHighlighter.hpp"
#include "ui/highlight/LingoHighlighter.hpp"

#include <cassert>
#include <QColor>

namespace {

// QSyntaxHighlighter stores its formats in the block's QTextLayout (the
// display layer), not in the document's character formats, so the test
// queries layout format ranges.
QTextCharFormat formatAt(QTextDocument& doc, int blockNumber, int pos) {
    const QTextLayout* layout = doc.findBlockByNumber(blockNumber).layout();
    assert(layout != nullptr);
    for (const auto& range : layout->formats()) {
        if (pos >= range.start && pos < range.start + range.length) {
            return range.format;
        }
    }
    return QTextCharFormat();
}

bool hasForeground(QTextDocument& doc, int blockNumber, int pos) {
    return formatAt(doc, blockNumber, pos).foreground().style() != Qt::NoBrush;
}

QColor charColor(QTextDocument& doc, int blockNumber, int pos) {
    const QTextCharFormat format = formatAt(doc, blockNumber, pos);
    assert(format.foreground().style() != Qt::NoBrush);
    return format.foreground().color();
}

int charWeight(QTextDocument& doc, int blockNumber, int pos) {
    return formatAt(doc, blockNumber, pos).fontWeight();
}

int underlineStyle(QTextDocument& doc, int blockNumber, int pos) {
    return formatAt(doc, blockNumber, pos).underlineStyle();
}

} // namespace

int main(int argc, char** argv) {
    // Headless: the highlighters pick their scheme from the app palette.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    using namespace libreshockwave::debugger;

    // ---- Lingo view ----
    {
        QTextDocument doc;
        LingoHighlighter highlighter(&doc);
        highlighter.setMethodNames({"foo"});
        doc.setPlainText(
            QStringLiteral("put \"hi there\" into lFoo\n"
                           "me.foo(#head, 42)\n"
                           "if lFoo = true then\n"
                           "> \u25B6 on foo(a)"));
        // The live app relies on the event loop to run the deferred
        // rehighlight; drive it directly for a deterministic test.
        highlighter.rehighlight();

        // Block 0: `put "hi there" into lFoo`
        //  p(0) ... "(4) h(5) ... "(13)  i(15) n t o  l(20) F o o
        assert(charColor(doc, 0, 0) == LingoHighlighter::keywordColor());
        assert(charColor(doc, 0, 5) == LingoHighlighter::stringColor());
        assert(charColor(doc, 0, 15) == LingoHighlighter::keywordColor());
        assert(!hasForeground(doc, 0, 20)); // lFoo is not a known symbol

        // Block 1: `me.foo(#head, 42)`
        //  m(0) e . f(3) o o ( # (7) h e a d ,   4(14) 2 )
        assert(charColor(doc, 1, 0) == LingoHighlighter::builtinColor());
        assert(charColor(doc, 1, 3) == LingoHighlighter::methodColor());
        assert(charColor(doc, 1, 7) == LingoHighlighter::symbolColor());
        assert(charColor(doc, 1, 14) == LingoHighlighter::numberColor());

        // Block 2: `if lFoo = true then`
        //  i(0) f   l(3) F o o   =   t(10) r u e   t(15) h e n
        assert(charColor(doc, 2, 0) == LingoHighlighter::keywordColor());
        assert(!hasForeground(doc, 2, 3));
        assert(charColor(doc, 2, 10) == LingoHighlighter::builtinColor());
        assert(charColor(doc, 2, 15) == LingoHighlighter::keywordColor());

        // Block 3: `> ▶ on foo(a)` — gutter marker + prefix.
        assert(charColor(doc, 3, 0) == GutterHighlighter::markerColor(false));
        assert(charWeight(doc, 3, 0) == static_cast<int>(QFont::Bold));
        assert(charColor(doc, 3, 2) == GutterHighlighter::markerColor(false));
        assert(charColor(doc, 3, 4) == LingoHighlighter::keywordColor());
        assert(charColor(doc, 3, 7) == LingoHighlighter::methodColor());

        // Updating the method set recolors without a document rebuild.
        highlighter.setMethodNames({"bar"});
        assert(!hasForeground(doc, 1, 3)); // foo is no longer a method
    }

    // ---- Lingo view: declaration / variable underlines ----
    {
        QTextDocument doc;
        LingoHighlighter highlighter(&doc);
        // "foo" and "speed" are movie-wide declarations (single underline).
        // "lfoo" is both a declaration and a current-handler variable, so the
        // dotted (variable) underline must win.
        highlighter.setDeclarationNames({"foo", "speed", "lfoo"});
        highlighter.setVariableNames({"lfoo"});
        doc.setPlainText(
            QStringLiteral("call foo()\n"      // foo   -> single underline
                           "put 1 into speed\n" // speed -> single underline
                           "put 2 into lFoo\n"  // lFoo  -> dotted underline (variable wins)
                           "put 3 into plain")); // plain -> no underline
        highlighter.rehighlight();

        // Block 0: `call foo()` — foo at index 5.
        assert(underlineStyle(doc, 0, 5) == QTextCharFormat::SingleUnderline);
        // Block 1: `put 1 into speed` — speed at index 11.
        assert(underlineStyle(doc, 1, 11) == QTextCharFormat::SingleUnderline);
        // Block 2: `put 2 into lFoo` — lFoo at index 11.
        assert(underlineStyle(doc, 2, 11) == QTextCharFormat::DotLine);
        // Block 3: `put 3 into plain` — plain at index 11, no underline.
        assert(underlineStyle(doc, 3, 11) == QTextCharFormat::NoUnderline);
    }

    // ---- Bytecode view ----
    {
        QTextDocument doc;
        BytecodeHighlighter highlighter(&doc);
        // Same layout CodeViewPanel builds: [> ](marker)NNNN  <opcode,16>
        // <arg,6> [  ; annotation].
        doc.setPlainText(
            QStringLiteral("  0000  pushZero             0  ; x\n"
                           "> \u25B6 0004  extCall              7  ; foo\n"
                           "  0008  setLocal             2\n"
                           "  0012  jmpIfZ               4\n"
                           "  0016  getGlobal            3\n"
                           "  0020  put                  5\n"));
        highlighter.rehighlight();

        // Block 0: offset at 2, opcode at 8, ';' of the annotation at 32.
        assert(charColor(doc, 0, 2) == BytecodeHighlighter::offsetColor());
        assert(charColor(doc, 0, 8) == BytecodeHighlighter::categoryColor("pushZero"));
        assert(charColor(doc, 0, 32) == BytecodeHighlighter::commentColor());

        // Block 1: "> " prefix + marker + call opcode (control category).
        assert(charColor(doc, 1, 0) == GutterHighlighter::markerColor(false));
        assert(charColor(doc, 1, 2) == GutterHighlighter::markerColor(false));
        assert(charColor(doc, 1, 4) == BytecodeHighlighter::offsetColor());
        assert(charColor(doc, 1, 10) == BytecodeHighlighter::categoryColor("extCall"));
        assert(charColor(doc, 1, 32) == BytecodeHighlighter::commentColor());

        // Block 2: set* opcode, no annotation.
        assert(charColor(doc, 2, 8) == BytecodeHighlighter::categoryColor("setLocal"));

        // Block 3: jmp* is control flow.
        assert(charColor(doc, 3, 8) == BytecodeHighlighter::categoryColor("jmpIfZ"));

        // Block 4: get* opcode.
        assert(charColor(doc, 4, 8) == BytecodeHighlighter::categoryColor("getGlobal"));

        // Block 5: put opcode.
        assert(charColor(doc, 5, 8) == BytecodeHighlighter::categoryColor("put"));
    }

    // ---- Bytecode view: declaration underline in the annotation ----
    {
        QTextDocument doc;
        BytecodeHighlighter highlighter(&doc);
        highlighter.setDeclarationNames({"foo"});
        doc.setPlainText(
            QStringLiteral("  0000  extCall              7  ; foo\n"
                           "  0004  pushZero             0  ; bar\n"));
        highlighter.rehighlight();

        // Block 0: annotation `; foo` — the identifier foo gets a single
        // underline. Block 1: `; bar` — bar is not a declaration. Positions are
        // computed from the rendered text so the assertion is layout-robust.
        const QTextBlock block0 = doc.findBlockByNumber(0);
        const int fooPos = block0.text().indexOf(QStringLiteral("; foo")) + 2;
        assert(fooPos > 0);
        assert(underlineStyle(doc, 0, fooPos) == QTextCharFormat::SingleUnderline);
        const QTextBlock block1 = doc.findBlockByNumber(1);
        const int barPos = block1.text().indexOf(QStringLiteral("; bar")) + 2;
        assert(barPos > 0);
        assert(underlineStyle(doc, 1, barPos) == QTextCharFormat::NoUnderline);
    }

    return 0;
}
