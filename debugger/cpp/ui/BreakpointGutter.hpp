#pragma once

#include <QEvent>
#include <QPlainTextEdit>
#include <QToolButton>
#include <vector>

namespace libreshockwave::debugger {

/// Left gutter column of the debugger Code view: one real Qt6 indicator widget
/// per code line (blank, current-line play marker, or breakpoint dot), shown
/// beside a tab's QPlainTextEdit.  Clicking an indicator toggles the breakpoint
/// for that line; the whole indicator column tracks the editor viewport.
///
/// The indicators are QToolButtons, not painted text, so they are real
/// clickable widgets that never interfere with the code document.  Row
/// geometry comes from the editor's own document layout, so indicators stay
/// pixel-aligned with their code lines while scrolling.
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
    /// gutter takes `editor`'s font so symbol and text widths match.
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

    /// Move the indicator column back to the top (called when code is cleared).
    void resetScroll();

    /// Position the indicator column as if the editor were scrolled to
    /// `value`; used to restore alignment across a code rebuild.
    void setScrollValue(int value);

signals:
    /// Emitted when an indicator is clicked; `row` is the code line index.
    void rowClicked(int row);

private:
    /// Recompute per-row geometry from the editor's document layout and move
    /// the indicator column to match the editor's current scroll position.
    void syncRows();

    /// Re-sync when the editor is shown or resized (its lines only get their
    /// real geometry once it is on screen) or its document changes.
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPlainTextEdit* editor_;
    QWidget* rows_;
    int gutterWidth_{0};
    int fallbackRowHeight_{0};
    std::vector<QToolButton*> cells_;
    std::vector<State> states_;
};

} // namespace libreshockwave::debugger
