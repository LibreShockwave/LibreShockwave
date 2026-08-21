#pragma once

#include <QPlainTextEdit>
#include <QTableWidget>
#include <QToolButton>
#include <vector>

namespace libreshockwave::debugger {

/// Left gutter column of the debugger Code view: one real Qt6 indicator widget
/// per code line (blank, current-line play marker, or breakpoint dot), shown
/// beside a tab's QPlainTextEdit.  Clicking an indicator toggles the breakpoint
/// for that line; the gutter scrolls in lockstep with the code viewport.
///
/// The indicators are QToolButtons, not painted text, so they are real
/// clickable widgets that never interfere with the code document and keep the
/// code lines pixel-aligned.
///
/// Usage:
///   BreakpointGutter gutter{view, parent};
///   layout->addWidget(&gutter);
///   gutter.rebuild(10, {State::Breakpoint, State::Blank, ...});
///   connect(&gutter, &BreakpointGutter::rowClicked, this, &App::onGutterClick);
class BreakpointGutter : public QWidget {
    Q_OBJECT

public:
    enum class State { Blank, Current, Breakpoint };

    /// Build a gutter whose rows line up with `editor`'s code lines.  The
    /// gutter takes `editor`'s font so symbol and text heights match.
    explicit BreakpointGutter(QPlainTextEdit* editor, QWidget* parent = nullptr);

    /// (Re)build the indicator for every row.  `states` must hold `rowCount`
    /// entries; a smaller count drops trailing rows, a larger count creates
    /// new indicator widgets.
    void rebuild(int rowCount, const std::vector<State>& states);

    /// Return the indicator currently shown for `row`, or `Blank` if `row` is
    /// outside the current row count.
    [[nodiscard]] State stateAt(int row) const;

    /// The QToolButton indicator for `row` (blank, current-line, or
    /// breakpoint), or nullptr if `row` is outside the current row count.
    [[nodiscard]] QToolButton* indicatorButton(int row) const;

    /// Reset the gutter scroll to the top (called whenever the code is cleared).
    void resetScroll();

    /// Set the gutter scroll position to `value` (keeps it in step with the
    /// code viewport across a rebuild).
    void setScrollValue(int value);

signals:
    /// Emitted when an indicator is clicked; `row` is the code line index.
    void rowClicked(int row);

private:
    QPlainTextEdit* editor_;
    QTableWidget* table_;
    int gutterWidth_{0};
    int rowHeight_{0};
    std::vector<QToolButton*> cells_;
    std::vector<State> states_;
};

} // namespace libreshockwave::debugger
