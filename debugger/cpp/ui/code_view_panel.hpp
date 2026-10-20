#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSet>
#include <QTabWidget>
#include <QWidget>
#include <set>
#include <string>
#include <vector>

#include "model/debugger_model.hpp"
#include "model/symbol_index.hpp"

class QMenu;
class QTimer;

namespace libreshockwave::debugger {

class BytecodeHighlighter;
class BreakpointGutter;
class LingoHighlighter;

/// Bottom panel showing bytecode and decompiled Lingo code with a breakpoint
/// gutter and current-line highlight, plus syntax highlighting and
/// right-click "Go to declaration" for methods, variables, properties, and
/// globals.
///
/// Each tab pairs its QPlainTextEdit with a BreakpointGutter: a Qt6 widget
/// column of one indicator per line (blank, current-line play marker, or
/// breakpoint dot), backed by real QToolButtons rather than painted text.
/// Clicking an indicator toggles a breakpoint at that instruction offset,
/// whether the movie is running or paused (matching the WASM harness).  The
/// gutter scrolls in lockstep with the code.  Right-clicking a word in either
/// view offers declaration jumps: handler calls resolve across the whole movie
/// (via the SymbolIndex), and local variables/arguments jump to their first
/// assignment in the current handler.
class CodeViewPanel : public QWidget {
    Q_OBJECT

public:
    explicit CodeViewPanel(QWidget* parent = nullptr);

    /// Load handler code for display.  `argNames` and `localNames` are the
    /// current handler's declared argument and local variable names (empty
    /// for the script overview); they enable variable go-to-declaration.
    void setHandlerCode(int cast_lib_number,
                        int script_id,
                        const std::string& script_name,
                        const std::string& handler_name,
                        const std::vector<InstructionData>& instructions,
                        const std::vector<DecompiledLineData>& decompiled_lines,
                        const std::vector<std::string>& handler_names,
                        const std::vector<std::string>& arg_names,
                        const std::vector<std::string>& local_names);

    /// Update current-line highlight from a pause snapshot.
    void setCurrentInstruction(int offset);

    /// Update which offsets have breakpoints set.
    void setBreakpointOffsets(const std::set<int>& offsets);

    /// Replace the movie-wide declaration index (methods, properties,
    /// globals) used by right-click "Go to declaration" and method-name
    /// highlighting.
    void setSymbolIndex(const SymbolIndex& index);

    /// Clear all displayed code.
    void clear();

    /// The bytecode and decompiled code views (the two tabs).
    [[nodiscard]] QPlainTextEdit* bytecodeView() const { return bytecode_view_; }
    [[nodiscard]] QPlainTextEdit* decompiledView() const { return decompiled_view_; }

    /// The gutter indicator column beside each code view.
    [[nodiscard]] BreakpointGutter* bytecodeGutter() const { return bytecode_gutter_; }
    [[nodiscard]] BreakpointGutter* decompiledGutter() const { return decompiled_gutter_; }

    /// Build the right-click context menu for `view` at `viewportPos`.  It
    /// always contains the standard text-editor actions (Undo / Cut / Copy /
    /// Paste / Select All); the "Go to declaration" options are appended only
    /// when a resolvable word is under the cursor.  The caller takes ownership
    /// of the returned menu; it deletes itself on close (Qt::WA_DeleteOnClose).
    /// The event filter execs the menu; tests build it directly without showing it.
    [[nodiscard]] QMenu* buildRightClickMenu(QPlainTextEdit* view,
                                              const QPoint& viewport_pos);

    /// Offset of the code line last clicked in the gutter column, or -1.
    /// Used by the window's F9 shortcut when the movie is running.
    [[nodiscard]] int lastClickedOffset() const { return last_clicked_offset_; }

signals:
    /// Emitted when the user toggles a breakpoint by clicking the gutter.
    void breakpointToggled(int script_id, const std::string& handler_name, int offset);

    /// Emitted when the user selects a different handler from the dropdown.
    void handlerChanged(const std::string& handler_name);

    /// Emitted when "Go to declaration" targets a script other than the one
    /// currently displayed (method, property, or global declaration).
    void goToDeclarationRequested(const DeclarationTarget& target);

protected:
    /// Intercepts left-clicks in the gutter column and the right-click
    /// QEvent::ContextMenu (replacing Qt's default text menu) for the
    /// declaration context menu of either code view.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onTabChanged(int index);
    void onHandlerComboChanged(int index);
    void clearFlash();

private:
    void rebuildDisplay();
    /// Map a gutter click at `row` for the bytecode/decompiled tab to its
    /// instruction offset and toggle a breakpoint there.
    void handleGutterRow(int row, bool is_bytecode);
    /// Build and exec the context menu for the word at `viewportPos` (viewport
    /// coordinates): the standard text-editor actions plus any "Go to
    /// declaration" options.  Always returns true; the caller accepts the
    /// context-menu event so this unified menu replaces Qt's default.
    [[nodiscard]] bool handleRightClick(QPlainTextEdit* view, const QPoint& viewport_pos);
    /// Scroll `view` so document line `line` is centered and flash it briefly.
    void revealLine(QPlainTextEdit* view, int line);
    /// After a tab switch, scroll the newly shown view to the code for the
    /// breakpoint offset last clicked in the other tab, flashing that line.
    void revealLastClickedOffset();
    /// Row of the instruction at exactly `offset`, or -1 when absent.
    [[nodiscard]] int bytecodeRowForOffset(int offset) const;
    /// Row of the decompiled line covering `offset` (the last line starting
    /// at or before it), or -1 when no line qualifies.
    [[nodiscard]] int decompiledRowForOffset(int offset) const;
    [[nodiscard]] int findVariableDeclarationLine(const QString& lower_name,
                                                  bool is_argument) const;
    /// Append the "Go to declaration" options for `word` to `menu`: a local
    /// variable or argument jumps to its first assignment in the current
    /// handler; methods, properties, and globals resolve across the movie.
    /// Only called when a word is under the cursor; adds nothing otherwise.
    void addDeclarationActions(QMenu* menu, QPlainTextEdit* view,
                               const QString& word);

    QTabWidget* tab_widget_;
    QWidget* bytecode_tab_{nullptr};     // page hosting the bytecode view
    QWidget* decompiled_tab_{nullptr};   // page hosting the decompiled view
    QPlainTextEdit* bytecode_view_;
    QPlainTextEdit* decompiled_view_;
    QComboBox* handler_combo_;
    QLabel* info_label_;           // QLabel showing handler info
    BytecodeHighlighter* bytecode_highlighter_{nullptr};
    LingoHighlighter* lingo_highlighter_{nullptr};
    QTimer* flash_timer_{nullptr};
    BreakpointGutter* bytecode_gutter_{nullptr};
    BreakpointGutter* decompiled_gutter_{nullptr};

    std::string script_name_;
    std::string current_handler_name_;
    int current_cast_lib_number_{0};
    int current_script_id_{0};
    std::vector<InstructionData> instructions_;
    std::vector<DecompiledLineData> decompiled_lines_;
    std::vector<std::string> handler_names_;
    QSet<QString> arg_names_;      // lower-cased current-handler argument names
    QSet<QString> local_names_;    // lower-cased current-handler local names
    SymbolIndex symbol_index_;
    std::set<int> breakpoint_offsets_;
    int current_instruction_offset_{-1};
    int last_clicked_offset_{-1};
};

} // namespace libreshockwave::debugger
