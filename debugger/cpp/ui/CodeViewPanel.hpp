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

#include "model/DebuggerModel.hpp"
#include "model/SymbolIndex.hpp"

class QMenu;
class QTimer;

namespace libreshockwave::debugger {

class BytecodeHighlighter;
class LingoHighlighter;

/// Bottom panel showing bytecode and decompiled Lingo code with breakpoint
/// gutter and current-line highlighting, plus syntax highlighting and
/// right-click "Go to declaration" for methods, variables, properties, and
/// globals.
///
/// Breakpoint toggling matches the WASM harness: clicking the gutter column
/// of a code line toggles a breakpoint at that instruction offset, whether
/// the movie is running or paused.  Right-clicking a word in either view
/// offers declaration jumps: handler calls resolve across the whole movie
/// (via the SymbolIndex), and local variables/arguments jump to their first
/// assignment in the current handler.
class CodeViewPanel : public QWidget {
    Q_OBJECT

public:
    explicit CodeViewPanel(QWidget* parent = nullptr);

    /// Load handler code for display.  `argNames` and `localNames` are the
    /// current handler's declared argument and local variable names (empty
    /// for the script overview); they enable variable go-to-declaration.
    void setHandlerCode(int castLibNumber,
                        int scriptId,
                        const std::string& scriptName,
                        const std::string& handlerName,
                        const std::vector<InstructionData>& instructions,
                        const std::vector<DecompiledLineData>& decompiledLines,
                        const std::vector<std::string>& handlerNames,
                        const std::vector<std::string>& argNames,
                        const std::vector<std::string>& localNames);

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
    [[nodiscard]] QPlainTextEdit* bytecodeView() const { return bytecodeView_; }
    [[nodiscard]] QPlainTextEdit* decompiledView() const { return decompiledView_; }

    /// Build the right-click context menu for `view` at `viewportPos`.  It
    /// always contains the standard text-editor actions (Undo / Cut / Copy /
    /// Paste / Select All); the "Go to declaration" options are appended only
    /// when a resolvable word is under the cursor.  The caller takes ownership
    /// of the returned menu; it deletes itself on close (Qt::WA_DeleteOnClose).
    /// The event filter execs the menu; tests build it directly without showing it.
    [[nodiscard]] QMenu* buildRightClickMenu(QPlainTextEdit* view,
                                              const QPoint& viewportPos);

    /// Offset of the code line last clicked in the gutter column, or -1.
    /// Used by the window's F9 shortcut when the movie is running.
    [[nodiscard]] int lastClickedOffset() const { return lastClickedOffset_; }

signals:
    /// Emitted when the user toggles a breakpoint by clicking the gutter.
    void breakpointToggled(int scriptId, const std::string& handlerName, int offset);

    /// Emitted when the user selects a different handler from the dropdown.
    void handlerChanged(const std::string& handlerName);

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
    void handleGutterClick(QPlainTextEdit* view, const QPoint& viewportPos,
                            bool isBytecode);
    /// Build and exec the context menu for the word at `viewportPos` (viewport
    /// coordinates): the standard text-editor actions plus any "Go to
    /// declaration" options.  Always returns true; the caller accepts the
    /// context-menu event so this unified menu replaces Qt's default.
    [[nodiscard]] bool handleRightClick(QPlainTextEdit* view, const QPoint& viewportPos);
    void jumpToDecompiledLine(int line);
    [[nodiscard]] int findVariableDeclarationLine(const QString& lowerName,
                                                  bool isArgument) const;
    /// Append the "Go to declaration" options for `word` to `menu`: a local
    /// variable or argument jumps to its first assignment in the current
    /// handler; methods, properties, and globals resolve across the movie.
    /// Only called when a word is under the cursor; adds nothing otherwise.
    void addDeclarationActions(QMenu* menu, QPlainTextEdit* view,
                               const QString& word);

    QTabWidget* tabWidget_;
    QPlainTextEdit* bytecodeView_;
    QPlainTextEdit* decompiledView_;
    QComboBox* handlerCombo_;
    QLabel* infoLabel_;           // QLabel showing handler info
    BytecodeHighlighter* bytecodeHighlighter_{nullptr};
    LingoHighlighter* lingoHighlighter_{nullptr};
    QTimer* flashTimer_{nullptr};

    std::string scriptName_;
    std::string currentHandlerName_;
    int currentCastLibNumber_{0};
    int currentScriptId_{0};
    std::vector<InstructionData> instructions_;
    std::vector<DecompiledLineData> decompiledLines_;
    std::vector<std::string> handlerNames_;
    QSet<QString> argNames_;      // lower-cased current-handler argument names
    QSet<QString> localNames_;    // lower-cased current-handler local names
    SymbolIndex symbolIndex_;
    std::set<int> breakpointOffsets_;
    int currentInstructionOffset_{-1};
    int lastClickedOffset_{-1};
};

} // namespace libreshockwave::debugger
