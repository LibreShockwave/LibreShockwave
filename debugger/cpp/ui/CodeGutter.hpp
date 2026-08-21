#pragma once

#include <QPlainTextEdit>
#include <QTableWidget>
#include <vector>

namespace libreshockwave::debugger {

/// Left gutter column of the debugger Code view: one real Qt6 indicator widget
/// per code line (blank, current-line play marker, or breakpoint dot), shown
/// beside a tab's QPlainTextEdit. Clicking an indicator toggles the breakpoint
/// for that line; the gutter scrolls in lockstep with the code viewport.
///
/// Usage:
///   CodeGutter gutter{view, parent};
///   layout->addWidget(&gutter);
///   gutter.rebuild(10, {Indicator::Breakpoint, Indicator::Blank, ...});
///   connect(&gutter, &CodeGutter::indicatorClicked, this, &App::onGutterClick);
class CodeGutter : public QWidget {
    Q_OBJECT

public:
    enum class Indicator { Blank, Current, Breakpoint };

    /// Build a gutter whose rows line up with `editor`'s code lines. The
    /// gutter takes `editor`'s font so symbol and text heights match.
    explicit CodeGutter(QPlainTextEdit* editor, QWidget* parent = nullptr);

    /// (Re)build the indicator for every row. `modes` must hold `rowCount`
    /// entries; a smaller count drops trailing rows, a larger count creates
    /// new indicator widgets.
    void rebuild(int rowCount, const std::vector<Indicator>& modes);

    /// Return the indicator currently shown for `row`, or `Blank` if `row` is
    /// outside the current row count.
    [[nodiscard]] Indicator modeAt(int row) const;

    /// Reset the gutter scroll to the top (called whenever the code is cleared).
    void resetScroll();

    /// Set the gutter scroll position to `value` (keeps it in step with the
    /// code viewport across a rebuild).
    void setScrollValue(int value);

signals:
    /// Emitted when an indicator is clicked; `row` is the code line index.
    void indicatorClicked(int row);

private:
    QPlainTextEdit* editor_;
    QTableWidget* table_;
    int gutterWidth_{0};
    int lineHeight_{0};
    std::vector<QWidget*> cells_; // each is an IndicatorCell
    std::vector<Indicator> modes_;
};

} // namespace libreshockwave::debugger
