#include "BreakpointGutter.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QPalette>
#include <QScrollBar>
#include <QString>
#include <QToolButton>

namespace libreshockwave::debugger {

namespace {

// Extra horizontal margin inside the gutter column beyond the widest symbol.
constexpr int kGutterPadding = 12;

// Threshold on the editor base-palette luminance for the light/dark choice.
constexpr int kLightBaseThreshold = 128;

// The color of an indicator glyph, chosen from the editor's base color to fit
// a light or dark theme.  Red wins on a breakpoint, amber on the current line.
QColor indicatorColor(BreakpointGutter::State state) {
    const bool dark =
        qGray(QGuiApplication::palette().color(QPalette::Base).rgba()) < kLightBaseThreshold;
    if (state == BreakpointGutter::State::Breakpoint) {
        return dark ? QColor(0xf8, 0x71, 0x71) : QColor(0xdc, 0x26, 0x26);
    }
    return dark ? QColor(0xfb, 0xbf, 0x24) : QColor(0xd9, 0x77, 0x06);
}

// The glyph drawn for each indicator: nothing when blank, a play arrow on the
// current line, and a dot on a breakpoint.
QString glyph(BreakpointGutter::State state) {
    switch (state) {
        case BreakpointGutter::State::Current:
            return QString(QChar(0x25B6));
        case BreakpointGutter::State::Breakpoint:
            return QString(QChar(0x25CF));
        case BreakpointGutter::State::Blank:
            return QString();
    }
    return QString();
}

} // namespace

BreakpointGutter::BreakpointGutter(QPlainTextEdit* editor, QWidget* parent)
    : QWidget(parent),
      editor_(editor) {

    table_ = new QTableWidget(0, 1, this);
    table_->setFont(editor->font());
    QFontMetrics metrics(editor->font());
    gutterWidth_ = metrics.horizontalAdvance(QLatin1String("●")) + kGutterPadding;
    rowHeight_ = metrics.lineSpacing();
    table_->setFixedWidth(gutterWidth_);
    table_->setColumnWidth(0, gutterWidth_);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setVisible(false);
    table_->setShowGrid(false);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Keep the gutter scroll in lockstep with the code viewport.
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged,
            table_->verticalScrollBar(), &QScrollBar::setValue);
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged,
            editor_->verticalScrollBar(), &QScrollBar::setValue);
}

void BreakpointGutter::rebuild(int rowCount, const std::vector<State>& states) {
    if (rowCount < 0) {
        rowCount = 0;
    }

    while (table_->rowCount() > rowCount) {
        const int row = table_->rowCount() - 1;
        delete table_->cellWidget(row, 0);
        table_->removeRow(row);
        if (!cells_.empty()) {
            cells_.pop_back();
        }
    }
    while (table_->rowCount() < rowCount) {
        const int row = table_->rowCount();
        table_->setRowCount(row + 1);
        table_->setRowHeight(row, rowHeight_);
        auto* button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setFixedSize(gutterWidth_, rowHeight_);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip("Toggle breakpoint");
        const int capturedRow = row;
        connect(button, &QToolButton::clicked, this, [this, capturedRow]() {
            emit rowClicked(capturedRow);
        });
        table_->setCellWidget(row, 0, button);
        cells_.push_back(button);
    }

    states_ = states;
    for (int row = 0; row < rowCount; ++row) {
        const State state =
            (row < static_cast<int>(states.size())) ? states[row] : State::Blank;
        QToolButton* button = cells_[row];
        button->setText(glyph(state));
        // Transparent face keeps blank rows invisible; the glyph color carries
        // the theme, and hover/pressed add a subtle highlight for the clickable
        // area without painting a box over the rest of the gutter.
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton { color: #%1; background: transparent; border: none; } "
                "QToolButton:hover { background: rgba(128,128,128,48); } "
                "QToolButton:pressed { background: rgba(128,128,128,96); } ")
                .arg(indicatorColor(state).name(QColor::HexArgb)));
    }
}

BreakpointGutter::State BreakpointGutter::stateAt(int row) const {
    if (row < 0 || row >= static_cast<int>(states_.size())) {
        return State::Blank;
    }
    return states_[row];
}

QToolButton* BreakpointGutter::indicatorButton(int row) const {
    if (row < 0 || row >= static_cast<int>(cells_.size())) {
        return nullptr;
    }
    return cells_[row];
}

void BreakpointGutter::resetScroll() {
    table_->verticalScrollBar()->setValue(0);
}

void BreakpointGutter::setScrollValue(int value) {
    table_->verticalScrollBar()->setValue(value);
}

} // namespace libreshockwave::debugger
