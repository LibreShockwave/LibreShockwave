#include "BreakpointGutter.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPalette>
#include <QScrollBar>
#include <QString>
#include <QTextBlock>
#include <QTextDocument>
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

    QFontMetrics metrics(editor->font());
    gutterWidth_ = metrics.horizontalAdvance(QLatin1String("●")) + kGutterPadding;
    fallbackRowHeight_ = metrics.lineSpacing();
    setFixedWidth(gutterWidth_);

    // All indicators live in one absolutely-positioned container; scrolling
    // the editor just moves this container, so alignment costs O(1) per tick.
    rows_ = new QWidget(this);
    rows_->resize(gutterWidth_, 0);

    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this]() { syncRows(); });
    connect(editor_->document(), &QTextDocument::contentsChanged, this,
            [this]() { syncRows(); });
    editor_->installEventFilter(this);
}

bool BreakpointGutter::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_ &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize)) {
        syncRows();
    }
    return QWidget::eventFilter(watched, event);
}

void BreakpointGutter::rebuild(int rowCount, const std::vector<State>& states) {
    if (rowCount < 0) {
        rowCount = 0;
    }

    while (static_cast<int>(cells_.size()) > rowCount) {
        delete cells_.back();
        cells_.pop_back();
    }
    while (static_cast<int>(cells_.size()) < rowCount) {
        const int capturedRow = static_cast<int>(cells_.size());
        auto* button = new QToolButton(rows_);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip("Toggle breakpoint");
        connect(button, &QToolButton::clicked, this, [this, capturedRow]() {
            emit rowClicked(capturedRow);
        });
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
        // name() (not HexArgb): QSS rejects 8-digit hex and drops the color.
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton { color: #%1; background: transparent; border: none; } "
                "QToolButton:hover { background: rgba(128,128,128,48); } "
                "QToolButton:pressed { background: rgba(128,128,128,96); } ")
                .arg(indicatorColor(state).name()));
    }

    syncRows();
    // The editor's line geometry may not be final until after the current
    // event-loop turn (polish, tab activation); re-sync once it settles.
    QMetaObject::invokeMethod(this, [this]() { syncRows(); },
                              Qt::QueuedConnection);
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
    rows_->move(0, 0);
}

void BreakpointGutter::setScrollValue(int value) {
    rows_->move(0, -value);
}

void BreakpointGutter::syncRows() {
    const int n = static_cast<int>(cells_.size());

    // Fallback grid from the editor font: frame + document margin offset the
    // first line, then one fallback row height per line.
    const int baseY =
        editor_->frameWidth() + editor_->document()->documentMargin();
    std::vector<int> tops(static_cast<size_t>(n), 0);
    std::vector<int> heights(static_cast<size_t>(n), fallbackRowHeight_);
    for (int i = 0; i < n; ++i) {
        tops[static_cast<size_t>(i)] = baseY + i * fallbackRowHeight_;
    }

    // Prefer the editor's laid-out geometry when it is on screen: each row
    // anchors to its own line's cursor rect (plus the scroll it was measured
    // at), so indicator and code line share one source of truth.
    QTextDocument* doc = editor_->document();
    if (editor_->isVisible() && n > 0) {
        const int scroll = editor_->verticalScrollBar()->value();
        bool allValid = true;
        for (int i = 0; i < n; ++i) {
            const QTextBlock block = doc->findBlockByLineNumber(i);
            if (!block.isValid()) {
                allValid = false;
                break;
            }
            tops[static_cast<size_t>(i)] =
                editor_->cursorRect(QTextCursor(block)).top() + scroll;
        }
        if (allValid) {
            for (int i = 0; i + 1 < n; ++i) {
                heights[static_cast<size_t>(i)] =
                    tops[static_cast<size_t>(i + 1)] - tops[static_cast<size_t>(i)];
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        cells_[static_cast<size_t>(i)]->setGeometry(
            0, tops[static_cast<size_t>(i)], gutterWidth_,
            heights[static_cast<size_t>(i)]);
    }
    rows_->resize(gutterWidth_, n > 0 ? tops[static_cast<size_t>(n - 1)] +
                                          heights[static_cast<size_t>(n - 1)]
                                      : 0);
    rows_->move(0, -editor_->verticalScrollBar()->value());
}

} // namespace libreshockwave::debugger
