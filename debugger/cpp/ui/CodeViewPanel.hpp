#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QWidget>
#include <set>
#include <string>
#include <vector>

#include "model/DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Bottom panel showing bytecode and decompiled Lingo code with breakpoint
/// gutter and current-line highlighting.
///
/// Breakpoint toggling matches the WASM harness: clicking the gutter column
/// of a code line toggles a breakpoint at that instruction offset, whether
/// the movie is running or paused.
class CodeViewPanel : public QWidget {
    Q_OBJECT

public:
    explicit CodeViewPanel(QWidget* parent = nullptr);

    /// Load handler code for display.
    void setHandlerCode(int scriptId,
                        const std::string& scriptName,
                        const std::string& handlerName,
                        const std::vector<InstructionData>& instructions,
                        const std::vector<DecompiledLineData>& decompiledLines,
                        const std::vector<std::string>& handlerNames);

    /// Update current-line highlight from a pause snapshot.
    void setCurrentInstruction(int offset);

    /// Update which offsets have breakpoints set.
    void setBreakpointOffsets(const std::set<int>& offsets);

    /// Clear all displayed code.
    void clear();

    /// Offset of the code line last clicked in the gutter column, or -1.
    /// Used by the window's F9 shortcut when the movie is running.
    [[nodiscard]] int lastClickedOffset() const { return lastClickedOffset_; }

signals:
    /// Emitted when the user toggles a breakpoint by clicking the gutter.
    void breakpointToggled(int scriptId, const std::string& handlerName, int offset);

    /// Emitted when the user selects a different handler from the dropdown.
    void handlerChanged(const std::string& handlerName);

protected:
    /// Intercepts left-clicks in the gutter column of either code view.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onTabChanged(int index);
    void onHandlerComboChanged(int index);

private:
    void rebuildDisplay();
    void handleGutterClick(QPlainTextEdit* view, const QPoint& viewportPos,
                           bool isBytecode);

    QTabWidget* tabWidget_;
    QPlainTextEdit* bytecodeView_;
    QPlainTextEdit* decompiledView_;
    QComboBox* handlerCombo_;
    QLabel* infoLabel_;           // QLabel showing handler info

    std::string scriptName_;
    std::string currentHandlerName_;
    int currentScriptId_{0};
    std::vector<InstructionData> instructions_;
    std::vector<DecompiledLineData> decompiledLines_;
    std::vector<std::string> handlerNames_;
    std::set<int> breakpointOffsets_;
    int currentInstructionOffset_{-1};
    int lastClickedOffset_{-1};
};

} // namespace libreshockwave::debugger
