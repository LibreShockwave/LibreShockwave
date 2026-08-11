#include "DebugWindowPanel.hpp"

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
    auto* toggleRow = new QHBoxLayout();
    toggle_ = new QCheckBox(QStringLiteral("Debug Enabled"), this);
    connect(toggle_, &QCheckBox::toggled, this,
            [this](bool checked) {
                setDebugEnabled(checked);
                emit debugEnabledChanged(checked);
            });
    toggleRow->addWidget(toggle_);
    stateLabel_ = new QLabel(this);
    toggleRow->addWidget(stateLabel_);
    toggleRow->addStretch();
    layout->addLayout(toggleRow);

    // Debug print log
    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setFont(QFont(QStringLiteral("monospace"), 10));
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView_->setMaximumBlockCount(2000);
    layout->addWidget(logView_, 1);

    // Clear button
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    connect(clearButton_, &QPushButton::clicked, this, &DebugWindowPanel::clearLog);
    layout->addWidget(clearButton_, 0, Qt::AlignRight);

    setDebugEnabled(true);
}

bool DebugWindowPanel::debugEnabled() const { return enabled_; }

void DebugWindowPanel::setDebugEnabled(bool enabled) {
    enabled_ = enabled;
    if (toggle_ != nullptr && toggle_->isChecked() != enabled) {
        const QSignalBlocker blocker(toggle_);
        toggle_->setChecked(enabled);
    }
    stateLabel_->setText(enabled
        ? QStringLiteral("Debug output is on")
        : QStringLiteral("Debug output is off"));
}

void DebugWindowPanel::appendLogLine(const QString& tag, const QString& text) {
    if (!enabled_) {
        return;
    }
    logView_->appendPlainText(
        QStringLiteral("[%1] %2").arg(tag, text));
}

void DebugWindowPanel::clearLog() {
    logView_->clear();
}

} // namespace libreshockwave::debugger
