#include "CodeGutter.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QScrollBar>

namespace libreshockwave::debugger {

namespace {

// Threshold on the editor base-palette luminance for the light/dark choice.
constexpr int kLightBaseThreshold = 128;

// Breakpoint dot wins the visual when a line is both current and has a
// breakpoint, matching the previous in-text marker precedence.
QColor indicatorColor(CodeGutter::Indicator mode) {
    const bool dark =
        qGray(QGuiApplication::palette().color(QPalette::Base).rgba()) < kLightBaseThreshold;
    if (mode == CodeGutter::Indicator::Breakpoint) {
        return dark ? QColor(0xf8, 0x71, 0x71) : QColor(0xdc, 0x26, 0x26);
    }
    return dark ? QColor(0xfb, 0xbf, 0x24) : QColor(0xd9, 0x77, 0x06);
}

// One gutter row: paints the current/breakpoint symbol or nothing, and turns a
// press+release inside the cell into a click so the breakpoint toggles.
class IndicatorCell : public QWidget {
    Q_OBJECT

public:
    explicit IndicatorCell(CodeGutter::Indicator mode, QWidget* parent)
        : QWidget(parent), mode_(mode) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setMode(CodeGutter::Indicator mode) {
        if (mode_ == mode) return;
        mode_ = mode;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        if (mode_ == CodeGutter::Indicator::Blank) {
            return;
        }
        QPainter painter(this);
        const QChar symbol =
            (mode_ == CodeGutter::Indicator::Current) ? QChar(0x25B6) : QChar(0x25CF);
        painter.setPen(indicatorColor(mode_));
        QRect content = rect().adjusted(3, 0, -3, 0);
        QTextOption option(Qt::AlignHCenter | Qt::AlignVCenter);
        painter.drawText(content, QString(symbol), option);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            pressPos_ = event->pos();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (pressed_ && event->button() == Qt::LeftButton &&
            rect().contains(event->pos()) && rect().contains(pressPos_)) {
            emit clicked();
        }
        pressed_ = false;
        update();
    }

private:
    CodeGutter::Indicator mode_;
    bool pressed_{false};
    QPoint pressPos_;
};

} // namespace

CodeGutter::CodeGutter(QPlainTextEdit* editor, QWidget* parent)
    : QWidget(parent),
      editor_(editor) {

    table_ = new QTableWidget(0, 1, this);
    table_->setFont(editor->font());
    QFontMetrics metrics(editor->font());
    gutterWidth_ = metrics.horizontalAdvance(QLatin1String("●")) + 6;
    lineHeight_ = metrics.lineSpacing();
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

void CodeGutter::rebuild(int rowCount, const std::vector<Indicator>& modes) {
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
        table_->setRowHeight(row, lineHeight_);
        const Indicator initial =
            (row < static_cast<int>(modes.size())) ? modes[row] : Indicator::Blank;
        auto* cell = new IndicatorCell(initial, this);
        const int capturedRow = row;
        connect(cell, &IndicatorCell::clicked, this, [this, capturedRow]() {
            emit indicatorClicked(capturedRow);
        });
        table_->setCellWidget(row, 0, cell);
        cells_.push_back(cell);
    }

    modes_ = modes;
    for (int row = 0; row < rowCount; ++row) {
        auto* cell = static_cast<IndicatorCell*>(cells_[row]);
        const Indicator m =
            (row < static_cast<int>(modes.size())) ? modes[row] : Indicator::Blank;
        cell->setMode(m);
    }
}

CodeGutter::Indicator CodeGutter::modeAt(int row) const {
    if (row < 0 || row >= static_cast<int>(modes_.size())) {
        return Indicator::Blank;
    }
    return modes_[row];
}

void CodeGutter::resetScroll() {
    table_->verticalScrollBar()->setValue(0);
}

void CodeGutter::setScrollValue(int value) {
    table_->verticalScrollBar()->setValue(value);
}

} // namespace libreshockwave::debugger

#include "CodeGutter.moc"
