#include "debug_window_panel.hpp"

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace libreshockwave::debugger {

DebugWindowPanel::DebugWindowPanel(QWidget* parent)
    : QWidget(parent) {

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Toggle row: native checkbox + plain status label
    auto* toggle_row = new QHBoxLayout();
    toggle_ = new QCheckBox(QStringLiteral("Debug Enabled"), this);
    connect(toggle_, &QCheckBox::toggled, this,
            [this](bool checked) {
                setDebugEnabled(checked);
                emit debugEnabledChanged(checked);
            });
    toggle_row->addWidget(toggle_);
    state_label_ = new QLabel(this);
    toggle_row->addWidget(state_label_);
    toggle_row->addStretch();
    layout->addLayout(toggle_row);

    // Debug print log
    log_view_ = new QPlainTextEdit(this);
    log_view_->setReadOnly(true);
    log_view_->setFont(QFont(QStringLiteral("monospace"), 10));
    log_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_view_->setMaximumBlockCount(2000);
    layout->addWidget(log_view_, 1);

    // Clear button
    clear_button_ = new QPushButton(QStringLiteral("Clear"), this);
    connect(clear_button_, &QPushButton::clicked, this, &DebugWindowPanel::clearLog);
    layout->addWidget(clear_button_, 0, Qt::AlignRight);

    setDebugEnabled(true);
}

bool DebugWindowPanel::debugEnabled() const { return enabled_; }

void DebugWindowPanel::setDebugEnabled(bool enabled) {
    enabled_ = enabled;
    if (toggle_ != nullptr && toggle_->isChecked() != enabled) {
        const QSignalBlocker blocker(toggle_);
        toggle_->setChecked(enabled);
    }
    state_label_->setText(enabled
        ? QStringLiteral("Debug output is on")
        : QStringLiteral("Debug output is off"));
}

void DebugWindowPanel::appendLogLine(const QString& tag, const QString& text) {
    if (!enabled_) {
        return;
    }
    log_view_->appendPlainText(
        QStringLiteral("[%1] %2").arg(tag, text));
}

void DebugWindowPanel::clearLog() {
    log_view_->clear();
}

} // namespace libreshockwave::debugger
