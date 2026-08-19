#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include "model/SymbolIndex.hpp"
#include "ui/CodeViewPanel.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace libreshockwave::debugger;

namespace {

// Viewport pixel inside the character at (lineIndex, charInLine) of `view`,
// so that view->cursorForPosition() (used inside buildRightClickMenu) maps it
// back to that character and WordUnderCursor selects the whole word.  Uses
// cursorRect() of a one-character selection, which returns viewport coords by
// construction and is font-agnostic.
QPoint charToViewport(QPlainTextEdit* view, int lineIndex, int charInLine) {
    QTextDocument* doc = view->document();
    const QTextBlock block = doc->findBlockByLineNumber(lineIndex);
    assert(block.isValid());
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + charInLine);
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

// A movie snapshot with one script (id 100) declaring methods "foo" and "bar",
// so right-clicking "bar" resolves to a method that is not the current handler.
MovieTreeSnapshot makeSnapshot() {
    MovieTreeSnapshot snapshot;
    MovieTreeSnapshot::MovieEntry movie;
    movie.castLibNumber = 1;
    MovieTreeSnapshot::ScriptEntry script;
    script.scriptId = 100;
    script.castLibNumber = 1;
    script.displayName = "MovieScript 1";
    script.handlers = {{"foo"}, {"bar"}};
    movie.scripts.push_back(script);
    snapshot.movies.push_back(movie);
    return snapshot;
}

} // namespace

int main(int argc, char** argv) {
    // Headless: the panel and its document layout render offscreen.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ---- Current-line marker must not light up -1 (structural) lines ----
    {
        CodeViewPanel panel;
        DecompiledLineData l0;
        l0.text = "on foo";
        l0.bytecodeOffset = -1;
        DecompiledLineData l1;
        l1.text = "put 1 into x";
        l1.bytecodeOffset = 0;
        DecompiledLineData l2;
        l2.text = "end";
        l2.bytecodeOffset = 2;
        std::vector<DecompiledLineData> lines = {l0, l1, l2};
        panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"}, {}, {});

        // Nothing is current (offset -1): no line may carry the "> " prefix.
        {
            const QStringList rows = panel.decompiledView()->toPlainText().split('\n');
            assert(rows.size() >= 3);
            assert(!rows[0].startsWith("> "));
            assert(!rows[1].startsWith("> "));
            assert(!rows[2].startsWith("> "));
        }

        // Offset 0 current: only that line carries the "> " prefix.
        panel.setCurrentInstruction(0);
        {
            const QStringList rows = panel.decompiledView()->toPlainText().split('\n');
            assert(!rows[0].startsWith("> "));
            assert(rows[1].startsWith("> "));
            assert(!rows[2].startsWith("> "));
        }
    }

    // ---- Right-click "Go to declaration" menu ----
    {
        CodeViewPanel panel;
        panel.setSymbolIndex(SymbolIndex{makeSnapshot()});
        panel.resize(600, 400);
        panel.show();
        // The decompiled view is the second tab; make it the visible one so its
        // document layout is valid for cursorRect()/cursorForPosition().
        QTabWidget* tabs = panel.findChild<QTabWidget*>();
        assert(tabs != nullptr);
        tabs->setCurrentWidget(panel.decompiledView());
        QApplication::processEvents();

        // Method "bar" (a different handler) -> "Go to method".
        {
            DecompiledLineData l0;
            l0.text = "call bar";
            l0.bytecodeOffset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines,
                                 {"foo", "bar"}, {}, {});
            QApplication::processEvents();
            // View line is "  call bar"; "bar" starts at char 7.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 7);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menuHas(menu, "Go to method"));
            delete menu;
        }

        // Local variable "vTotal" -> "Go to variable declaration".
        {
            DecompiledLineData l0;
            l0.text = "put 1 into vTotal";
            l0.bytecodeOffset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"},
                                 {}, {"vTotal"});
            QApplication::processEvents();
            // View line is "  put 1 into vTotal"; "vTotal" starts at char 13.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 13);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menuHas(menu, "Go to variable declaration"));
            delete menu;
        }

        // Undeclared word -> "No declaration found".
        {
            DecompiledLineData l0;
            l0.text = "call qux";
            l0.bytecodeOffset = 0;
            std::vector<DecompiledLineData> lines = {l0};
            panel.setHandlerCode(1, 100, "MovieScript 1", "foo", {}, lines, {"foo"},
                                 {}, {});
            QApplication::processEvents();
            // View line is "  call qux"; "qux" starts at char 7.
            const QPoint pos = charToViewport(panel.decompiledView(), 0, 7);
            QMenu* menu = panel.buildRightClickMenu(panel.decompiledView(), pos);
            assert(menu != nullptr);
            assert(menu->actions().size() == 1);
            assert(menu->actions().first()->text() == "No declaration found");
            assert(!menu->actions().first()->isEnabled());
            delete menu;
        }
    }

    return 0;
}
