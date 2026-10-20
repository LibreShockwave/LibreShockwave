// Desktop debugger unit tests in one binary: startup movie-source selection,
// symbol index lookups, Lingo/bytecode syntax highlighting, code view panel
// gutters/menus/reveal behavior, and stage widget key-code forwarding.
// Add new debugger coverage here instead of creating another test target.
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>

#include "startup_session.hpp"
#include "model/symbol_index.hpp"
#include "ui/breakpoint_gutter.hpp"
#include "ui/code_view_panel.hpp"
#include "ui/stage_widget.hpp"
#include "ui/highlight/bytecode_highlighter.hpp"
#include "ui/highlight/lingo_highlighter.hpp"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

using namespace libreshockwave::debugger;

namespace {

constexpr int k_key_down = 3;
constexpr int k_key_up = 4;

// QSyntaxHighlighter stores its formats in the block's QTextLayout (the
// display layer), not in the document's character formats, so the test
// queries layout format ranges.
QTextCharFormat formatAt(QTextDocument& doc, int block_number, int pos) {
    const QTextLayout* layout = doc.findBlockByNumber(block_number).layout();
    assert(layout != nullptr);
    for (const auto& range : layout->formats()) {
        if (pos >= range.start && pos < range.start + range.length) {
            return range.format;
        }
    }
    return QTextCharFormat();
}

bool hasForeground(QTextDocument& doc, int block_number, int pos) {
    return formatAt(doc, block_number, pos).foreground().style() != Qt::NoBrush;
}

QColor charColor(QTextDocument& doc, int block_number, int pos) {
    const QTextCharFormat format = formatAt(doc, block_number, pos);
    assert(format.foreground().style() != Qt::NoBrush);
    return format.foreground().color();
}

int underlineStyle(QTextDocument& doc, int block_number, int pos) {
    return formatAt(doc, block_number, pos).underlineStyle();
}

MovieTreeSnapshot makeSymbolSnapshot() {
    MovieTreeSnapshot snapshot;
    MovieTreeSnapshot::MovieEntry movie;
    movie.cast_lib_number = 1;
    movie.name = "test.cct";

    MovieTreeSnapshot::ScriptEntry main_script;
    main_script.script_id = 1;
    main_script.cast_lib_number = 1;
    main_script.display_name = "MovieScript 1";
    main_script.handlers = {{"handleMouseUp"}, {"foo"}};
    main_script.property_names = {"speed"};
    main_script.global_names = {"gCount"};
    movie.scripts.push_back(main_script);

    MovieTreeSnapshot::ScriptEntry behavior;
    behavior.script_id = 2;
    behavior.cast_lib_number = 1;
    behavior.display_name = "Figure Class";
    behavior.handlers = {{"foo"}, {"draw"}};
    behavior.property_names = {"color"};
    movie.scripts.push_back(behavior);

    snapshot.movies.push_back(std::move(movie));
    return snapshot;
}

// Viewport pixel inside the character at (lineIndex, charInLine) of `view`,
// so that view->cursorForPosition() (used inside buildRightClickMenu) maps it
// back to that character and WordUnderCursor selects the whole word.  Uses
// cursorRect() of a one-character selection, which returns viewport coords by
// construction and is font-agnostic.
QPoint charToViewport(QPlainTextEdit* view, int line_index, int char_in_line) {
    QTextDocument* doc = view->document();
    const QTextBlock block = doc->findBlockByLineNumber(line_index);
    assert(block.isValid());
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + char_in_line);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    const QRect rect = view->cursorRect(cursor);
    assert(!rect.isEmpty());
    return rect.center();
}

// True when any menu action's text contains `needle`.
bool menuHas(QMenu* menu, const QString& needle) {
    for (QAction* action : menu->actions()) {
        if (action->text().contains(needle)) {
            return true;
        }
    }
    return false;
}

// Click the gutter's click indicator at pixel `pos`, exercising the real
// QToolButton whose clicked signal toggles a breakpoint.  Clicking the button
// itself (not the gutter) delivers the event headlessly, where QTest posts to
// the named widget rather than hit-testing to its children.
void clickIndicator(QToolButton* button, QPoint pos) {
    QTest::mouseClick(button, Qt::LeftButton, Qt::KeyboardModifiers(), pos);
}

// A movie snapshot with one script (id 100) declaring methods "foo" and "bar",
// so right-clicking "bar" resolves to a method that is not the current handler.
MovieTreeSnapshot makeCodeViewSnapshot() {
    MovieTreeSnapshot snapshot;
    MovieTreeSnapshot::MovieEntry movie;
    movie.cast_lib_number = 1;
    MovieTreeSnapshot::ScriptEntry script;
    script.script_id = 100;
    script.cast_lib_number = 1;
    script.display_name = "MovieScript 1";
    script.handlers = {{"foo"}, {"bar"}};
    movie.scripts.push_back(script);
    snapshot.movies.push_back(movie);
    return snapshot;
}

struct CapturedKey {
    int type;
    int key_code;
    std::string text;
};

class EventSpy {
public:
    void record(int type, int stage_x, int stage_y, int key_code,
                const std::string& key_text, bool /*shift*/, bool /*ctrl*/,
                bool /*alt*/, bool /*rightButton*/) {
        (void)stage_x;
        (void)stage_y;
        captured_.push_back(CapturedKey{type, key_code, key_text});
    }

    const std::vector<CapturedKey>& keys() const { return captured_; }
    void clear() { captured_.clear(); }

private:
    std::vector<CapturedKey> captured_;
};

void attachSpy(StageWidget& widget, EventSpy& spy) {
    widget.setInputCallback(
        [&spy](int type, int stage_x, int stage_y, int key_code,
               const std::string& key_text, bool shift, bool ctrl, bool alt,
               bool right_button) {
            spy.record(type, stage_x, stage_y, key_code, key_text, shift, ctrl,
                       alt, right_button);
        });
}

void sendKey(StageWidget& widget, int qt_key, const QString& text) {
    QKeyEvent press(QEvent::KeyPress, qt_key, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, qt_key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QApplication::sendEvent(&widget, &release);
}

} // namespace

void testStartupSessionSourceSelection() {
    assert(selectStartupMovieSource(QStringLiteral("movie.dir"),
                                       QStringLiteral("session.lswdebug")) ==
           QStringLiteral("movie.dir"));
    assert(selectStartupMovieSource(QString(), QStringLiteral("movie.dcr")) ==
           QStringLiteral("movie.dcr"));
    assert(selectStartupMovieSource(QStringLiteral("session.lswdebug"),
                                       QStringLiteral("other.lswdebug"))
               .isEmpty());
    assert(selectStartupMovieSource(QStringLiteral("SESSION.LSWDEBUG"),
                                       QStringLiteral("movie.cst")) ==
           QStringLiteral("movie.cst"));
}

void testSymbolIndex() {
    // Empty index.
    SymbolIndex empty;
    assert(empty.empty());
    assert(empty.find("foo", 1).empty());
    assert(empty.methodNames().empty());

    SymbolIndex index{makeSymbolSnapshot()};
    assert(!index.empty());

    // Method lookup prefers the given script, snapshot order otherwise.
    std::vector<DeclarationTarget> methods = index.find("foo", 2);
    assert(methods.size() == 2);
    assert(methods[0].kind == DeclarationKind::Method);
    assert(methods[0].cast_lib_number == 1);
    assert(methods[0].script_id == 2);
    assert(methods[0].script_name == "Figure Class");
    assert(methods[0].handler_name == "foo");
    assert(methods[1].script_id == 1);
    assert(methods[1].script_name == "MovieScript 1");

    // Lookup is case-insensitive; preference flips the order.
    methods = index.find("FOO", 1);
    assert(methods.size() == 2);
    assert(methods[0].script_id == 1);
    assert(methods[1].script_id == 2);

    // Property and global targets carry no handler name.
    auto properties = index.find("speed", 0);
    assert(properties.size() == 1);
    assert(properties[0].kind == DeclarationKind::Property);
    assert(properties[0].script_id == 1);
    assert(properties[0].handler_name.empty());

    auto globals = index.find("gcount", 0);
    assert(globals.size() == 1);
    assert(globals[0].kind == DeclarationKind::Global);
    assert(globals[0].script_id == 1);

    // Unknown names resolve to nothing.
    assert(index.find("missing", 1).empty());

    // Method names: lower-cased, snapshot order, duplicates removed.
    const std::vector<std::string>& names = index.methodNames();
    assert(names.size() == 3);
    assert(names[0] == "handlemouseup");
    assert(names[1] == "foo");
    assert(names[2] == "draw");

    // Declaration names: methods + properties + globals, lower-cased, snapshot
    // order, duplicates removed.  These are the words the code view underlines
    // as right-click "Go to declaration" targets.
    const std::vector<std::string>& declarations = index.declarationNames();
    assert(declarations.size() == 6);
    assert(declarations[0] == "handlemouseup");
    assert(declarations[1] == "foo");
    assert(declarations[2] == "speed");
    assert(declarations[3] == "gcount");
    assert(declarations[4] == "draw");
    assert(declarations[5] == "color");
    // An empty index has no declaration names.
    assert(empty.declarationNames().empty());

    // Scripts sharing an ID across cast libraries stay distinct.
    MovieTreeSnapshot multi = makeSymbolSnapshot();
    MovieTreeSnapshot::MovieEntry second;
    second.cast_lib_number = 2;
    second.name = "extra.cct";
    MovieTreeSnapshot::ScriptEntry extra;
    extra.script_id = 1; // same ID as the first movie's main script
    extra.cast_lib_number = 2;
    extra.display_name = "MovieScript 2";
    extra.handlers = {{"foo"}};
    second.scripts.push_back(extra);
    multi.movies.push_back(std::move(second));

    SymbolIndex multi_index{multi};
    methods = multi_index.find("foo", 0);
    assert(methods.size() == 3);
    assert(methods[0].cast_lib_number == 1 && methods[0].script_id == 1);
    assert(methods[1].cast_lib_number == 1 && methods[1].script_id == 2);
    assert(methods[2].cast_lib_number == 2 && methods[2].script_id == 1);
}

void testHighlighters() {
    // ---- Lingo view ----
    {
        QTextDocument doc;
        LingoHighlighter highlighter(&doc);
        highlighter.setMethodNames({"foo"});
        doc.setPlainText(
            QStringLiteral("put \"hi there\" into lFoo\n"
                           "me.foo(#head, 42)\n"
                           "if lFoo = true then\n"
                            "on foo(a)"));
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

        // Block 3: `on foo(a)` — handler keyword and method.
        assert(charColor(doc, 3, 0) == LingoHighlighter::keywordColor());
        assert(charColor(doc, 3, 3) == LingoHighlighter::methodColor());

        // Updating the method set recolors without a document rebuild.
        highlighter.setMethodNames({"bar"});
        assert(!hasForeground(doc, 1, 3)); // foo is no longer a method
    }

    // ---- Lingo view: declaration / variable underline data ----
    {
        QTextDocument doc;
        LingoHighlighter highlighter(&doc);
        // Declaration names are retained for lookup even when underlines are
        // disabled by the internal debugger switch.
        highlighter.setDeclarationNames({"foo", "speed", "lfoo"});
        highlighter.setVariableNames({"lfoo"});
        doc.setPlainText(
            QStringLiteral("call foo()\n"      // foo   -> single underline
                           "put 1 into speed\n" // speed -> single underline
                           "put 2 into lFoo\n"  // lFoo  -> dotted underline (variable wins)
                           "put 3 into plain")); // plain -> no underline
        highlighter.rehighlight();

        // Declaration and variable names are not underlined while disabled.
        assert(underlineStyle(doc, 0, 5) == QTextCharFormat::NoUnderline);
        assert(underlineStyle(doc, 1, 11) == QTextCharFormat::NoUnderline);
        assert(underlineStyle(doc, 2, 11) == QTextCharFormat::NoUnderline);
        // Block 3: `put 3 into plain` — plain at index 11, no underline.
        assert(underlineStyle(doc, 3, 11) == QTextCharFormat::NoUnderline);
    }

    // ---- Bytecode view ----
    {
        QTextDocument doc;
        BytecodeHighlighter highlighter(&doc);
        // Same layout CodeViewPanel builds: NNNN  <opcode,16><arg,6>[  ; annotation].
        doc.setPlainText(
            QStringLiteral("0000  pushZero             0  ; x\n"
                           "0004  extCall              7  ; foo\n"
                           "0008  setLocal             2\n"
                           "0012  jmpIfZ               4\n"
                           "0016  getGlobal            3\n"
                           "0020  put                  5\n"));
        highlighter.rehighlight();

        // Block 0: offset at 0, opcode at 6, ';' of the annotation at 30.
        assert(charColor(doc, 0, 0) == BytecodeHighlighter::offsetColor());
        assert(charColor(doc, 0, 6) == BytecodeHighlighter::categoryColor("pushZero"));
        assert(charColor(doc, 0, 30) == BytecodeHighlighter::commentColor());

        // Block 1: call opcode (control category) at 6, annotation at 30.
        assert(charColor(doc, 1, 0) == BytecodeHighlighter::offsetColor());
        assert(charColor(doc, 1, 6) == BytecodeHighlighter::categoryColor("extCall"));
        assert(charColor(doc, 1, 30) == BytecodeHighlighter::commentColor());

        // Block 2: set* opcode, no annotation.
        assert(charColor(doc, 2, 6) == BytecodeHighlighter::categoryColor("setLocal"));

        // Block 3: jmp* is control flow.
        assert(charColor(doc, 3, 6) == BytecodeHighlighter::categoryColor("jmpIfZ"));

        // Block 4: get* opcode.
        assert(charColor(doc, 4, 6) == BytecodeHighlighter::categoryColor("getGlobal"));

        // Block 5: put opcode.
        assert(charColor(doc, 5, 6) == BytecodeHighlighter::categoryColor("put"));
    }

    // ---- Bytecode view: declaration underline data in the annotation ----
    {
        QTextDocument doc;
        BytecodeHighlighter highlighter(&doc);
        highlighter.setDeclarationNames({"foo"});
        doc.setPlainText(
            QStringLiteral("  0000  extCall              7  ; foo\n"
                           "  0004  pushZero             0  ; bar\n"));
        highlighter.rehighlight();

        // Declaration names remain loaded for lookup, but are not underlined
        // while the internal declaration-highlighting switch is disabled.
        const QTextBlock block0 = doc.findBlockByNumber(0);
        const int foo_pos = block0.text().indexOf(QStringLiteral("; foo")) + 2;
        assert(foo_pos > 0);
        assert(underlineStyle(doc, 0, foo_pos) == QTextCharFormat::NoUnderline);
        const QTextBlock block1 = doc.findBlockByNumber(1);
        const int bar_pos = block1.text().indexOf(QStringLiteral("; bar")) + 2;
        assert(bar_pos > 0);
        assert(underlineStyle(doc, 1, bar_pos) == QTextCharFormat::NoUnderline);
    }
}

void testCodeViewPanel() {
    // ---- Current-line marker must not light up -1 (structural) lines ----
    {
        CodeViewPanel panel;
        DecompiledLineData l0;
        l0.text = "on foo";
        l0.bytecode_offset = -1;
        DecompiledLineData l1;
        l1.text = "put 1 into x";
        l1.bytecode_offset = 0;
        DecompiledLineData l2;
        l2.text = "end";
        l2.bytecode_offset = 2;
        std::vector<DecompiledLineData> lines = {l0, l1, l2};
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"}, {}, {});

        // Nothing is current (offset -1): no line carries a current indicator.
        {
            assert(panel.decompiledGutter()->stateAt(0) ==
                   BreakpointGutter::State::Blank);
            assert(panel.decompiledGutter()->stateAt(1) ==
                   BreakpointGutter::State::Blank);
            assert(panel.decompiledGutter()->stateAt(2) ==
                   BreakpointGutter::State::Blank);
        }

        // Offset 0 current: only that line carries the current indicator.
        panel.setCurrentInstruction(0);
        {
            assert(panel.decompiledGutter()->stateAt(0) ==
                   BreakpointGutter::State::Blank);
            assert(panel.decompiledGutter()->stateAt(1) ==
                   BreakpointGutter::State::Current);
            assert(panel.decompiledGutter()->stateAt(2) ==
                   BreakpointGutter::State::Blank);
        }
    }

    // ---- A breakpoint is keyed by offset, so it maps to a different gutter
    // row in the bytecode view than in the decompiled view: the per-tab scroll
    // position differs, but the breakpoint's logical identity (offset) is the
    // same.  It shows in both gutters and wins over the current marker.
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        {
            InstructionData i;
            i.offset = 0;
            i.opcode = "pushZero";
            i.argument = 0;
            instrs.push_back(i);
        }
        {
            InstructionData i;
            i.offset = 4;
            i.opcode = "put";
            i.argument = 1;
            instrs.push_back(i);
        }
        {
            InstructionData i;
            i.offset = 8;
            i.opcode = "ret";
            i.argument = 0;
            instrs.push_back(i);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "on foo";
            l.bytecode_offset = -1;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "put 1 into x";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "put 2 into y";
            l.bytecode_offset = 4;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "end";
            l.bytecode_offset = 8;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", instrs, lines,
                             {"foo"}, {}, {});
        panel.setBreakpointOffsets({8});

        // Bytecode view: offset 8 is row 2.
        assert(panel.bytecodeGutter()->stateAt(2) ==
               BreakpointGutter::State::Breakpoint);
        // Decompiled view: the structural "on foo" row shifts offset 8 to row 3.
        assert(panel.decompiledGutter()->stateAt(3) ==
               BreakpointGutter::State::Breakpoint);

        // Breakpoint wins over the current-line marker in both tabs.
        panel.setCurrentInstruction(8);
        assert(panel.bytecodeGutter()->stateAt(2) ==
               BreakpointGutter::State::Breakpoint);
        assert(panel.decompiledGutter()->stateAt(3) ==
               BreakpointGutter::State::Breakpoint);
    }

    // ---- Clicking a gutter row places a breakpoint at that line's offset ----
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        {
            InstructionData i;
            i.offset = 0;
            i.opcode = "pushZero";
            instrs.push_back(i);
        }
        {
            InstructionData i;
            i.offset = 4;
            i.opcode = "ret";
            instrs.push_back(i);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "on foo";
            l.bytecode_offset = -1;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "push zero";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", instrs, lines,
                              {"foo"}, {}, {});

        int row_h = QFontMetrics(panel.bytecodeView()->font()).lineSpacing();

        int script_id = 0;
        std::string handler;
        int offset = -1;
        int clicks = 0;
        QObject::connect(&panel, &CodeViewPanel::breakpointToggled, &panel,
                [&](int s, const std::string& h, int o) {
                    script_id = s;
                    handler = h;
                    offset = o;
                    ++clicks;
                });

        // Clicking the bytecode gutter row for offset 0 toggles there.
        clickIndicator(panel.bytecodeGutter()->indicatorButton(0),
                          QPoint(5, row_h / 2));
        assert(clicks == 1);
        assert(script_id == 100);
        assert(handler == "foo");
        assert(offset == 0);

        // Clicking the decompiled gutter row 1 (offset 0), past row 0's
        // structural "on foo", toggles at the same offset.
        clickIndicator(panel.decompiledGutter()->indicatorButton(1),
                          QPoint(5, row_h / 2));
        assert(clicks == 2);
        assert(offset == 0);

        // Clicking a structural line (offset -1) toggles nothing.
        clickIndicator(panel.decompiledGutter()->indicatorButton(0),
                          QPoint(5, row_h / 2));
        assert(clicks == 2);
    }

    // ---- Without a handler loaded, clicking the gutter toggles nothing ----
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        {
            InstructionData i;
            i.offset = 0;
            i.opcode = "pushZero";
            instrs.push_back(i);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "push zero";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        // Overview (no handler): setHandlerCode with an empty handlerName.
        panel.setHandlerCode(1, 100, "MovieScript 1", "", instrs, lines, {}, {},
                              {});

        int row_h = QFontMetrics(panel.bytecodeView()->font()).lineSpacing();
        int clicks = 0;
        QObject::connect(&panel, &CodeViewPanel::breakpointToggled, &panel,
                [&](int, const std::string&, int) { ++clicks; });
        clickIndicator(panel.bytecodeGutter()->indicatorButton(0),
                          QPoint(5, row_h / 2));
        assert(clicks == 0);
    }

    // ---- Right-click "Go to declaration" menu ----
    {
        CodeViewPanel panel;
        panel.setSymbolIndex(SymbolIndex{makeCodeViewSnapshot()});
        panel.resize(600, 400);
        panel.show();
        // The decompiled view is the second tab; make it the visible one so its
        // document layout is valid for cursorRect()/cursorForPosition().
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        // Tab 1 is Decompiled (constructor order); setCurrentWidget() needs
        // the page widget, not the nested view.
        tabs->setCurrentIndex(1);
        QApplication::processEvents();

        // Method "bar" (a different handler) -> "Go to method".
        {
            DecompiledLineData l0;
            l0.text = "call bar";
            l0.bytecode_offset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines,
                                 {"foo", "bar"}, {}, {});
            QApplication::processEvents();
            // View line is "call bar"; "bar" starts at char 5.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 5);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menuHas(menu, "Go to method"));
            delete menu;
        }

        // Local variable "vTotal" -> "Go to variable declaration".
        {
            DecompiledLineData l0;
            l0.text = "put 1 into vTotal";
            l0.bytecode_offset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"},
                                  {}, {"vTotal"});
            QApplication::processEvents();
            // View line is "put 1 into vTotal"; "vTotal" starts at char 11.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 11);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menuHas(menu, "Go to variable declaration"));
            delete menu;
        }

        // Undeclared word -> no "Go to declaration" options, just the standard
        // text-editor menu (so "Select All" stands on its own).
        {
            DecompiledLineData l0;
            l0.text = "call qux";
            l0.bytecode_offset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"},
                                  {}, {});
            QApplication::processEvents();
            // View line is "call qux"; "qux" starts at char 5.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 5);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menuHas(menu, "Select All"));
            assert(!menuHas(menu, "Go to"));
            delete menu;
        }
    }

    // ---- Real right-click arrives as QEvent::ContextMenu ----
    // Qt's default text menu (Copy / Select All) is shown in response to
    // QEvent::ContextMenu, so the filter must accept that event to replace
    // the default with the declaration menu; with no word under the cursor
    // it must leave the event unhandled so the default menu still works.
    {
        struct MenuSpy : QObject {
            QStringList menus; // one joined-action-texts entry per shown menu
            bool eventFilter(QObject* o, QEvent* e) override {
                if (e->type() == QEvent::Show) {
                    if (auto* menu = qobject_cast<QMenu*>(o)) {
                        QStringList texts;
                        for (const auto* action : menu->actions()) {
                            if (action->isSeparator()) continue;
                            texts << action->text();
                        }
                        menus << texts.join('|');
                    }
                }
                return false;
            }
        };
        MenuSpy spy;
        QApplication::instance()->installEventFilter(&spy);

        CodeViewPanel panel;
        panel.setSymbolIndex(SymbolIndex{makeCodeViewSnapshot()});
        panel.resize(600, 400);
        panel.show();
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        // Tab 1 is Decompiled (constructor order); setCurrentWidget() needs
        // the page widget, not the nested view.
        tabs->setCurrentIndex(1);
        DecompiledLineData l0;
        l0.text = "call bar";
        l0.bytecode_offset = 0;
        std::vector<DecompiledLineData> lines = {l0};
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines,
                              {"foo", "bar"}, {}, {});
        QApplication::processEvents();

        // Send a QEvent::ContextMenu for `pos`. Each call arms its own timer so
        // the nested exec() loop (from buildRightClickMenu) is always closable —
        // one timer per shown menu.
        const auto show_context_menu = [&](const QPoint& pos, bool& accepted) {
            QTimer::singleShot(50, []() {
                if (QWidget* popup = QApplication::activePopupWidget()) {
                    popup->close();
                }
            });
            QContextMenuEvent local(QContextMenuEvent::Mouse, pos, pos);
            QApplication::sendEvent(panel.decompiledView()->viewport(), &local);
            accepted = local.isAccepted();
        };

        // Over the word "bar": the unified menu is shown (accepted), merging the
        // standard text-editor actions with the declaration option.
        {
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 5);
            bool accepted = false;
            show_context_menu(pos, accepted);
            assert(accepted);
            assert(spy.menus.size() == 1);
            assert(spy.menus.first().contains("Go to method"));
            assert(spy.menus.first().contains("Select All"));
        }

        // Over empty space (the trailing blank line): no word under the cursor,
        // so only the unified standard menu shows (still no "Go to declaration").
        {
            const QPoint pos = charToViewport(panel.decompiledView(), 1, 0);
            bool accepted = false;
            show_context_menu(pos, accepted);
            assert(accepted);
            assert(spy.menus.size() == 2);
            assert(spy.menus.last().contains("Select All"));
            assert(!spy.menus.last().contains("Go to method"));
            assert(!spy.menus.last().contains("No declaration found"));
        }
        QApplication::instance()->removeEventFilter(&spy);
    }

    // ---- The gutter column is visible and paints its indicators ----
    // Regression: the gutter widget once had no internal layout, so the tab
    // layout assigned it zero width and no indicator could ever appear.
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        {
            InstructionData i;
            i.offset = 0;
            i.opcode = "pushZero";
            instrs.push_back(i);
        }
        {
            InstructionData i;
            i.offset = 4;
            i.opcode = "ret";
            instrs.push_back(i);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "on foo";
            l.bytecode_offset = -1;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "put 1 into x";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", instrs, lines,
                              {"foo"}, {}, {});
        panel.setBreakpointOffsets({0});
        panel.resize(600, 400);
        panel.show();
        QApplication::processEvents();

        BreakpointGutter* gutter = panel.bytecodeGutter();
        assert(gutter->isVisible());
        assert(gutter->width() > 0);
        QToolButton* bp = gutter->indicatorButton(0);
        assert(bp != nullptr);
        assert(bp->isVisible());
        assert(bp->width() == gutter->width());
        assert(gutter->indicatorButton(1) != nullptr);

        // The breakpoint glyph really reaches the screen: the grabbed panel
        // must contain pixels in the theme's breakpoint red (light 0xdc2626
        // or dark 0xf87171), and not just the default palette text color.
        const QImage img = panel.grab().toImage();
        int red_pixels = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.red() > 180 && c.red() - c.green() > 60 &&
                    c.red() - c.blue() > 60) {
                    ++red_pixels;
                }
            }
        }
        assert(red_pixels > 0);
    }

    // ---- Indicators stay pixel-aligned with their code lines ----
    // Each button's top in panel coordinates must match the mapped top of its
    // line's cursor rect (viewport truth), before and after scrolling.
    {
        CodeViewPanel panel;
        panel.resize(600, 400);
        panel.show();
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        // Tab 1 is Decompiled (constructor order); setCurrentWidget() needs
        // the page widget, not the nested view.
        tabs->setCurrentIndex(1);
        std::vector<DecompiledLineData> lines;
        for (int i = 0; i < 40; ++i) {
            DecompiledLineData l;
            l.text = QString("line %1").arg(i).toStdString();
            l.bytecode_offset = i * 4;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "h", {}, lines, {"h"}, {},
                              {});
        QApplication::processEvents();

        QPlainTextEdit* view = panel.decompiledView();
        QTextDocument* doc = view->document();
        BreakpointGutter* gutter = panel.decompiledGutter();

        const auto check_alignment = [&](int row) {
            const QRect line_rect = view->cursorRect(
                QTextCursor(doc->findBlockByLineNumber(row)));
            const int expected =
                view->viewport()->mapTo(&panel, QPoint(0, 0)).y() + line_rect.top();
            const int actual =
                gutter->indicatorButton(row)->mapTo(&panel, QPoint(0, 0)).y();
            assert(qAbs(actual - expected) <= 1);
        };

        check_alignment(0);
        check_alignment(5);
        check_alignment(10);

        // Scroll down: buttons must follow the viewport exactly.
        view->verticalScrollBar()->setValue(120);
        QApplication::processEvents();
        check_alignment(10);
        check_alignment(15);

        // A row scrolled off the top is no longer visible.
        assert(!gutter->indicatorButton(0)->isVisible());
    }

    // ---- Switching tabs reveals the line for the breakpoint just placed ----
    // A breakpoint clicked in the bytecode tab must scroll to and flash the
    // decompiled line that covers the same instruction offset, and vice versa.
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        for (int i = 0; i < 3; ++i) {
            InstructionData instr;
            instr.offset = i * 4;
            instr.opcode = "op";
            instrs.push_back(instr);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "on foo";
            l.bytecode_offset = -1;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "put 1 into x";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "put 2 into y";
            l.bytecode_offset = 4;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "end";
            l.bytecode_offset = 8;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", instrs, lines,
                              {"foo"}, {}, {});

        // Click the bytecode gutter row for offset 4, then open Decompiled:
        // the covering line ("put 2 into y", document line 2) flashes.
        clickIndicator(panel.bytecodeGutter()->indicatorButton(1),
                       QPoint(5, 2));
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        tabs->setCurrentIndex(1);
        QApplication::processEvents();

        const auto selections = panel.decompiledView()->extraSelections();
        assert(selections.size() == 1);
        assert(selections.first().cursor.blockNumber() == 2);

        // And back: a breakpoint clicked on decompiled line 1 (offset 0)
        // reveals bytecode row 0 when switching to the Bytecode tab.
        clickIndicator(panel.decompiledGutter()->indicatorButton(1),
                       QPoint(5, 2));
        tabs->setCurrentIndex(0);
        QApplication::processEvents();
        const auto bc_selections = panel.bytecodeView()->extraSelections();
        assert(bc_selections.size() == 1);
        assert(bc_selections.first().cursor.blockNumber() == 0);
    }

    // ---- A mid-statement bytecode offset reveals its whole statement ----
    // Offset 6 belongs to the statement starting at offset 0; switching to
    // Decompiled must land on that statement's line, not skip the reveal.
    {
        CodeViewPanel panel;
        std::vector<InstructionData> instrs;
        for (int i = 0; i < 4; ++i) {
            InstructionData instr;
            instr.offset = i * 2;
            instr.opcode = "op";
            instrs.push_back(instr);
        }
        std::vector<DecompiledLineData> lines;
        {
            DecompiledLineData l;
            l.text = "on foo";
            l.bytecode_offset = -1;
            lines.push_back(l);
        }
        {
            DecompiledLineData l;
            l.text = "one big statement";
            l.bytecode_offset = 0;
            lines.push_back(l);
        }
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", instrs, lines,
                              {"foo"}, {}, {});
        clickIndicator(panel.bytecodeGutter()->indicatorButton(3),
                       QPoint(5, 2));
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        tabs->setCurrentIndex(1);
        QApplication::processEvents();

        const auto selections = panel.decompiledView()->extraSelections();
        assert(selections.size() == 1);
        assert(selections.first().cursor.blockNumber() == 1);
    }
}

void testStageWidgetKeys() {
    // Enter and Backspace must arrive as Director codes 36 and 51; before the
    // fix they were forwarded as raw browser codes 13 and 8 and were ignored
    // by editable-field input handling.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_Return, QStringLiteral("\r"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].type == k_key_down && spy.keys()[0].key_code == 36);
        assert(spy.keys()[1].type == k_key_up && spy.keys()[1].key_code == 36);

        spy.clear();
        sendKey(widget, Qt::Key_Backspace, QStringLiteral("\b"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].type == k_key_down && spy.keys()[0].key_code == 51);
        assert(spy.keys()[1].type == k_key_up && spy.keys()[1].key_code == 51);
    }

    // Keypad Enter shares the Return code.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier);
        QApplication::sendEvent(&widget, &press);
        assert(spy.keys().size() == 1);
        assert(spy.keys()[0].key_code == 36);
    }

    // Navigation and editing keys use Director codes, not browser codes.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        const std::pair<int, int> arrow_cases[] = {
            {Qt::Key_Left, 123}, {Qt::Key_Right, 124}, {Qt::Key_Up, 126},
            {Qt::Key_Down, 125}, {Qt::Key_Tab, 48},    {Qt::Key_Escape, 53},
            {Qt::Key_Space, 49}, {Qt::Key_Delete, 117}, {Qt::Key_F1, 122},
        };
        for (const auto& [qt_key, director_code] : arrow_cases) {
            spy.clear();
            sendKey(widget, qt_key, QString());
            assert(spy.keys().size() == 2);
            assert(spy.keys()[0].type == k_key_down);
            assert(spy.keys()[0].key_code == director_code);
            assert(spy.keys()[1].key_code == director_code);
        }
    }

    // Printable keys keep their character text while the key code becomes the
    // Director Mac code (letter A -> 0, digit 5 -> 23).
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_A, QStringLiteral("a"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].key_code == 0);
        assert(spy.keys()[0].text == "a");

        spy.clear();
        sendKey(widget, Qt::Key_5, QStringLiteral("5"));
        assert(spy.keys()[0].key_code == 23);
        assert(spy.keys()[0].text == "5");
    }

    // Shifted punctuation (reported as its own Latin-1 key on Linux) must be
    // forwarded with its text; the code lands in the private printable range
    // instead of colliding with DOM navigation codes.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        const std::pair<int, QString> symbol_cases[] = {
            {Qt::Key_Exclam, QStringLiteral("!")},
            {Qt::Key_At, QStringLiteral("@")},
            {Qt::Key_NumberSign, QStringLiteral("#")},
            {Qt::Key_Percent, QStringLiteral("%")},
            {Qt::Key_Ampersand, QStringLiteral("&")},
            {Qt::Key_Asterisk, QStringLiteral("*")},
            {Qt::Key_ParenLeft, QStringLiteral("(")},
            {Qt::Key_Colon, QStringLiteral(":")},
            {Qt::Key_QuoteDbl, QStringLiteral("\"")},
            {Qt::Key_Less, QStringLiteral("<")},
            {Qt::Key_Greater, QStringLiteral(">")},
            {Qt::Key_Question, QStringLiteral("?")},
        };
        for (const auto& [qt_key, text] : symbol_cases) {
            spy.clear();
            sendKey(widget, qt_key, text);
            assert(spy.keys().size() == 2);
            const int expected_code = 0x2000 + qt_key;
            assert(spy.keys()[0].type == k_key_down);
            assert(spy.keys()[0].key_code == expected_code);
            assert(spy.keys()[0].text == text.toStdString());
            assert(spy.keys()[1].key_code == expected_code);
        }
    }

    // Keys with no mapping produce no callback events at all.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_Super_L, QString());
        assert(spy.keys().empty());
    }

    // Auto-repeat presses are suppressed like ordinary repeat handling.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        QKeyEvent repeat_press(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                              QStringLiteral("b"), true);
        QApplication::sendEvent(&widget, &repeat_press);
        assert(spy.keys().empty());
    }
}

int main(int argc, char** argv) {
    // Headless: the widgets and document layouts render offscreen.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    testStartupSessionSourceSelection();
    testSymbolIndex();
    testHighlighters();
    testCodeViewPanel();
    testStageWidgetKeys();
    return 0;
}
