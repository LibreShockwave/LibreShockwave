#include "CodeViewPanel.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QSplitter>
#include <QVBoxLayout>

namespace libreshockwave::debugger {

CodeViewPanel::CodeViewPanel(QWidget* parent)
    : QWidget(parent) {

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top bar: handler dropdown + info label
    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(4, 2, 4, 2);

    handlerCombo_ = new QComboBox(this);
    handlerCombo_->setMinimumWidth(200);
    handlerCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(handlerCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CodeViewPanel::onHandlerComboChanged);
    topBar->addWidget(handlerCombo_);

    infoLabel_ = new QLabel(this);
    topBar->addWidget(infoLabel_);
    topBar->addStretch();

    layout->addLayout(topBar);

    // Tab widget: bytecode / decompiled
    tabWidget_ = new QTabWidget(this);
    tabWidget_->setTabPosition(QTabWidget::North);

    bytecodeView_ = new QPlainTextEdit(this);
    bytecodeView_->setReadOnly(true);
    bytecodeView_->setFont(QFont(QStringLiteral("monospace"), 11));
    bytecodeView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    bytecodeView_->setTabStopDistance(32);
    tabWidget_->addTab(bytecodeView_, QStringLiteral("Bytecode"));

    decompiledView_ = new QPlainTextEdit(this);
    decompiledView_->setReadOnly(true);
    decompiledView_->setFont(QFont(QStringLiteral("monospace"), 11));
    decompiledView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tabWidget_->addTab(decompiledView_, QStringLiteral("Decompiled"));

    layout->addWidget(tabWidget_);

    connect(tabWidget_, &QTabWidget::currentChanged, this, &CodeViewPanel::onTabChanged);

    // Click-to-toggle breakpoints in the gutter column of either view.
    // The event filter is installed on the viewports (the widgets that
    // actually receive mouse events) so cursorForPosition() gets
    // viewport-relative coordinates.
    bytecodeView_->viewport()->installEventFilter(this);
    decompiledView_->viewport()->installEventFilter(this);
}

void CodeViewPanel::setHandlerCode(
    int scriptId,
    const std::string& scriptName,
    const std::string& handlerName,
    const std::vector<InstructionData>& instructions,
    const std::vector<DecompiledLineData>& decompiledLines,
    const std::vector<std::string>& handlerNames) {

    currentScriptId_ = scriptId;
    scriptName_ = scriptName;
    currentHandlerName_ = handlerName;
    instructions_ = instructions;
    decompiledLines_ = decompiledLines;
    handlerNames_ = handlerNames;
    lastClickedOffset_ = -1;

    // Update handler combo
    handlerCombo_->blockSignals(true);
    handlerCombo_->clear();
    handlerCombo_->addItem(QStringLiteral("— Overview —"));
    for (const auto& h : handlerNames) {
        handlerCombo_->addItem(QString::fromStdString(h));
    }
    // Select the current handler
    if (!handlerName.empty()) {
        const int idx = handlerCombo_->findText(QString::fromStdString(handlerName));
        if (idx >= 0) handlerCombo_->setCurrentIndex(idx);
    }
    handlerCombo_->blockSignals(false);

    // Update info label
    infoLabel_->setText(
        QStringLiteral("%1 → %2").arg(
            QString::fromStdString(scriptName),
            handlerName.empty() ? QStringLiteral("Overview")
                                : QString::fromStdString(handlerName)));

    rebuildDisplay();
}

void CodeViewPanel::setCurrentInstruction(int offset) {
    currentInstructionOffset_ = offset;
    rebuildDisplay();
}

void CodeViewPanel::setBreakpointOffsets(const std::set<int>& offsets) {
    breakpointOffsets_ = offsets;
    rebuildDisplay();
}

void CodeViewPanel::clear() {
    currentScriptId_ = 0;
    scriptName_.clear();
    currentHandlerName_.clear();
    instructions_.clear();
    decompiledLines_.clear();
    handlerNames_.clear();
    breakpointOffsets_.clear();
    currentInstructionOffset_ = -1;
    lastClickedOffset_ = -1;
    handlerCombo_->clear();
    infoLabel_->clear();

    bytecodeView_->clear();
    decompiledView_->clear();
}

bool CodeViewPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (obj == bytecodeView_->viewport()) {
                handleGutterClick(bytecodeView_, mouse->pos(), true);
                return true;
            }
            if (obj == decompiledView_->viewport()) {
                handleGutterClick(decompiledView_, mouse->pos(), false);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CodeViewPanel::handleGutterClick(QPlainTextEdit* view, const QPoint& viewportPos,
                                      bool isBytecode) {
    // A handler must be displayed to toggle against (matches the WASM
    // harness, which uses the currently selected handler).
    if (currentScriptId_ <= 0 || currentHandlerName_.empty()) {
        return;
    }

    // Only clicks in the gutter column (marker + offset) toggle breakpoints,
    // so the rest of the line still supports text selection.
    const QFontMetrics metrics(view->font());
    const int gutterWidth = metrics.horizontalAdvance(QStringLiteral("  ")) +
                            metrics.horizontalAdvance(QStringLiteral("0000 ")) + 6;
    if (viewportPos.x() >= gutterWidth) {
        return;
    }

    const int line = view->cursorForPosition(viewportPos).blockNumber();
    int offset = -1;
    if (isBytecode) {
        if (line >= 0 && line < static_cast<int>(instructions_.size())) {
            offset = instructions_[line].offset;
        }
    } else {
        if (line >= 0 && line < static_cast<int>(decompiledLines_.size())) {
            offset = decompiledLines_[line].bytecodeOffset;
        }
    }
    if (offset < 0) {
        return;
    }

    lastClickedOffset_ = offset;
    emit breakpointToggled(currentScriptId_, currentHandlerName_, offset);
}

void CodeViewPanel::onTabChanged(int /*index*/) {
    rebuildDisplay();
}

void CodeViewPanel::onHandlerComboChanged(int index) {
    if (index < 0) return;
    const auto name = handlerCombo_->itemText(index).toStdString();
    if (name != currentHandlerName_) {
        emit handlerChanged(name);
    }
}

void CodeViewPanel::rebuildDisplay() {
    // Build bytecode text
    {
        QString text;
        for (const auto& instr : instructions_) {
            const bool isCurrent = (instr.offset == currentInstructionOffset_);
            const bool hasBp = breakpointOffsets_.count(instr.offset) > 0;

            QString line;
            // Gutter marker
            if (isCurrent) {
                line += QStringLiteral("▶ ");
            } else if (hasBp) {
                line += QStringLiteral("● ");
            } else {
                line += QStringLiteral("  ");
            }

            // Offset
            line += QStringLiteral("%1  ").arg(instr.offset, 4);

            // Opcode
            line += QStringLiteral("%1").arg(
                QString::fromStdString(instr.opcode), -16);

            // Argument
            line += QStringLiteral("%1").arg(instr.argument, 6);

            // Annotation
            if (!instr.annotation.empty()) {
                line += QStringLiteral("  ; %1").arg(
                    QString::fromStdString(instr.annotation));
            }

            if (isCurrent) {
                line = QStringLiteral("> %1").arg(line);
            }

            text += line + QStringLiteral("\n");
        }
        bytecodeView_->setPlainText(text);
    }

    // Build decompiled text
    {
        QString text;
        for (const auto& line : decompiledLines_) {
            const bool isCurrent = (line.bytecodeOffset == currentInstructionOffset_);
            const bool hasBp = breakpointOffsets_.count(line.bytecodeOffset) > 0;

            QString displayLine;
            if (isCurrent) {
                displayLine += QStringLiteral("▶ ");
            } else if (hasBp) {
                displayLine += QStringLiteral("● ");
            } else {
                displayLine += QStringLiteral("  ");
            }

            displayLine += QString::fromStdString(line.text);

            if (isCurrent) {
                displayLine = QStringLiteral("> %1").arg(displayLine);
            }

            text += displayLine + QStringLiteral("\n");
        }
        decompiledView_->setPlainText(text);
    }
}

} // namespace libreshockwave::debugger
