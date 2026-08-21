#include "CodeViewPanel.hpp"

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/BreakpointGutter.hpp"
#include "ui/highlight/BytecodeHighlighter.hpp"
#include "ui/highlight/LingoHighlighter.hpp"

namespace libreshockwave::debugger {

namespace {

// Flash shown after a go-to-declaration jump fades out after this long.
constexpr int kFlashDurationMs = 1500;

// Amber flash background (semi-transparent) for the jumped-to line.
const QColor kFlashBackground(0xff, 0xd5, 0x4f, 0x5c);

} // namespace

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

    // Tab widget: bytecode / decompiled.  Each tab pairs its code view with a
    // gutter of per-line indicator widgets to its left.
    tabWidget_ = new QTabWidget(this);
    tabWidget_->setTabPosition(QTabWidget::North);

    bytecodeView_ = new QPlainTextEdit(this);
    bytecodeView_->setReadOnly(true);
    bytecodeView_->setFont(QFont(QStringLiteral("monospace"), 11));
    bytecodeView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    bytecodeView_->setTabStopDistance(32);
    bytecodeHighlighter_ = new BytecodeHighlighter(bytecodeView_->document());
    {
        auto* bytecodeTab = new QWidget(this);
        auto* bytecodeLayout = new QHBoxLayout(bytecodeTab);
        bytecodeLayout->setContentsMargins(0, 0, 0, 0);
        bytecodeGutter_ = new BreakpointGutter(bytecodeView_, bytecodeTab);
        connect(bytecodeGutter_, &BreakpointGutter::rowClicked, this,
                [this](int row) { handleGutterRow(row, true); });
        bytecodeLayout->addWidget(bytecodeGutter_);
        bytecodeLayout->addWidget(bytecodeView_, 1);
        tabWidget_->addTab(bytecodeTab, QStringLiteral("Bytecode"));
    }

    decompiledView_ = new QPlainTextEdit(this);
    decompiledView_->setReadOnly(true);
    decompiledView_->setFont(QFont(QStringLiteral("monospace"), 11));
    decompiledView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    lingoHighlighter_ = new LingoHighlighter(decompiledView_->document());
    {
        auto* decompiledTab = new QWidget(this);
        auto* decompiledLayout = new QHBoxLayout(decompiledTab);
        decompiledLayout->setContentsMargins(0, 0, 0, 0);
        decompiledGutter_ = new BreakpointGutter(decompiledView_, decompiledTab);
        connect(decompiledGutter_, &BreakpointGutter::rowClicked, this,
                [this](int row) { handleGutterRow(row, false); });
        decompiledLayout->addWidget(decompiledGutter_);
        decompiledLayout->addWidget(decompiledView_, 1);
        tabWidget_->addTab(decompiledTab, QStringLiteral("Decompiled"));
    }

    flashTimer_ = new QTimer(this);
    flashTimer_->setSingleShot(true);
    flashTimer_->setInterval(kFlashDurationMs);
    connect(flashTimer_, &QTimer::timeout, this, &CodeViewPanel::clearFlash);

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
    int castLibNumber,
    int scriptId,
    const std::string& scriptName,
    const std::string& handlerName,
    const std::vector<InstructionData>& instructions,
    const std::vector<DecompiledLineData>& decompiledLines,
    const std::vector<std::string>& handlerNames,
    const std::vector<std::string>& argNames,
    const std::vector<std::string>& localNames) {

    currentCastLibNumber_ = castLibNumber;
    currentScriptId_ = scriptId;
    scriptName_ = scriptName;
    currentHandlerName_ = handlerName;
    instructions_ = instructions;
    decompiledLines_ = decompiledLines;
    handlerNames_ = handlerNames;
    lastClickedOffset_ = -1;

    argNames_.clear();
    localNames_.clear();
    for (const auto& name : argNames) {
        argNames_.insert(QString::fromStdString(name).toLower());
    }
    for (const auto& name : localNames) {
        localNames_.insert(QString::fromStdString(name).toLower());
    }
    QSet<QString> variables = argNames_;
    variables.unite(localNames_);
    lingoHighlighter_->setVariableNames(variables);

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

void CodeViewPanel::setSymbolIndex(const SymbolIndex& index) {
    symbolIndex_ = index;
    QSet<QString> methods;
    QSet<QString> declarations;
    for (const auto& name : index.methodNames()) {
        methods.insert(QString::fromStdString(name));
    }
    for (const auto& name : index.declarationNames()) {
        declarations.insert(QString::fromStdString(name));
    }
    lingoHighlighter_->setMethodNames(methods);
    lingoHighlighter_->setDeclarationNames(declarations);
    bytecodeHighlighter_->setDeclarationNames(declarations);
}

void CodeViewPanel::clear() {
    currentCastLibNumber_ = 0;
    currentScriptId_ = 0;
    scriptName_.clear();
    currentHandlerName_.clear();
    instructions_.clear();
    decompiledLines_.clear();
    handlerNames_.clear();
    argNames_.clear();
    localNames_.clear();
    symbolIndex_ = SymbolIndex();
    breakpointOffsets_.clear();
    currentInstructionOffset_ = -1;
    lastClickedOffset_ = -1;
    handlerCombo_->clear();
    infoLabel_->clear();
    lingoHighlighter_->setMethodNames({});
    lingoHighlighter_->setDeclarationNames({});
    lingoHighlighter_->setVariableNames({});
    bytecodeHighlighter_->setDeclarationNames({});
    clearFlash();

    bytecodeView_->clear();
    decompiledView_->clear();
    bytecodeGutter_->rebuild(0, {});
    decompiledGutter_->rebuild(0, {});
}

bool CodeViewPanel::eventFilter(QObject* obj, QEvent* event) {
    // Qt's default text menu (Copy / Select All) is shown for a QEvent::ContextMenu,
    // a separate event from the right-button release.  Replace it for either code
    // view with the unified menu (standard actions + "Go to declaration" options).
    if (event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        if (obj == bytecodeView_->viewport() &&
            handleRightClick(bytecodeView_, ce->pos())) {
            event->accept();
            return true;
        }
        if (obj == decompiledView_->viewport() &&
            handleRightClick(decompiledView_, ce->pos())) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CodeViewPanel::handleGutterRow(int row, bool isBytecode) {
    // A handler must be displayed to toggle against (matches the WASM
    // harness, which uses the currently selected handler).
    if (currentScriptId_ <= 0 || currentHandlerName_.empty()) {
        return;
    }

    int offset = -1;
    if (isBytecode) {
        if (row >= 0 && row < static_cast<int>(instructions_.size())) {
            offset = instructions_[row].offset;
        }
    } else {
        if (row >= 0 && row < static_cast<int>(decompiledLines_.size())) {
            offset = decompiledLines_[row].bytecodeOffset;
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

bool CodeViewPanel::handleRightClick(QPlainTextEdit* view, const QPoint& viewportPos) {
    QMenu* menu = buildRightClickMenu(view, viewportPos);
    // WA_DeleteOnClose: the menu deletes itself once exec() returns.
    menu->exec(view->viewport()->mapToGlobal(viewportPos));
    return true;
}

QMenu* CodeViewPanel::buildRightClickMenu(QPlainTextEdit* view,
                                          const QPoint& viewportPos) {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // The standard text-editor actions (Undo / Cut / Copy / Paste / Select All),
    // wired to the editor so the shortcuts and read-only enabled state behave
    // exactly like Qt's default context menu.
    const bool writable = !view->isReadOnly();
    menu->addAction(tr("&Undo"), QKeySequence::Undo, view, &QPlainTextEdit::undo)
        ->setEnabled(writable);
    menu->addAction(tr("&Redo"), QKeySequence::Redo, view, &QPlainTextEdit::redo)
        ->setEnabled(writable);
    menu->addSeparator();
    menu->addAction(tr("Cu&t"), QKeySequence::Cut, view, &QPlainTextEdit::cut)
        ->setEnabled(writable);
    menu->addAction(tr("&Copy"), QKeySequence::Copy, view, &QPlainTextEdit::copy);
    menu->addAction(tr("&Paste"), QKeySequence::Paste, view, &QPlainTextEdit::paste)
        ->setEnabled(writable);
    menu->addSeparator();
    menu->addAction(tr("&Select All"), QKeySequence::SelectAll, view,
                    &QPlainTextEdit::selectAll);

    // Go-to-declaration options, added only when a resolvable word is under the
    // cursor; dropped otherwise so the normal menu stands on its own.
    QTextCursor cursor = view->cursorForPosition(viewportPos);
    cursor.select(QTextCursor::WordUnderCursor);
    const QString word = cursor.selectedText();
    if (!word.isEmpty()) {
        menu->addSeparator();
        addDeclarationActions(menu, view, word);
    }
    return menu;
}

void CodeViewPanel::addDeclarationActions(QMenu* menu, QPlainTextEdit* view,
                                          const QString& word) {
    if (view == decompiledView_) {
        // Local variables and arguments declared in the current handler
        // jump to their first assignment in this handler's decompiled code.
        const QString lower = word.toLower();
        if (argNames_.contains(lower) || localNames_.contains(lower)) {
            const bool isArgument = argNames_.contains(lower);
            const int line = findVariableDeclarationLine(lower, isArgument);
            if (line >= 0) {
                const int targetLine = line;
                const auto* action = menu->addAction(
                    QStringLiteral("Go to %1 declaration (line %2)")
                        .arg(isArgument ? QStringLiteral("argument")
                                        : QStringLiteral("variable"),
                             QString::number(line + 1)));
                connect(action, &QAction::triggered, this,
                        [this, targetLine]() {
                            jumpToDecompiledLine(targetLine);
                        });
            }
        }
    }

    // Methods, properties, and globals resolve across the whole movie.
    if (!symbolIndex_.empty()) {
        const auto targets = symbolIndex_.find(word.toStdString(), currentScriptId_);
        for (const auto& target : targets) {
            const QString label =
                target.kind == DeclarationKind::Method
                    ? QStringLiteral("Go to method")
                    : (target.kind == DeclarationKind::Property
                           ? QStringLiteral("Go to property")
                           : QStringLiteral("Go to global"));
            const bool isCurrent =
                target.kind == DeclarationKind::Method &&
                target.castLibNumber == currentCastLibNumber_ &&
                target.scriptId == currentScriptId_ &&
                target.handlerName == currentHandlerName_;
            if (isCurrent) {
                menu->addAction(
                        QStringLiteral("%1 (%2) — current")
                            .arg(label, QString::fromStdString(target.scriptName)))
                    ->setEnabled(false);
                continue;
            }
            const DeclarationTarget targetCopy = target;
            menu->addAction(
                QStringLiteral("%1 (%2)").arg(
                    label, QString::fromStdString(target.scriptName)));
            connect(menu->actions().last(), &QAction::triggered, this,
                    [this, targetCopy]() {
                        emit goToDeclarationRequested(targetCopy);
                    });
        }
    }
}

int CodeViewPanel::findVariableDeclarationLine(const QString& lowerName,
                                               bool isArgument) const {
    const auto wordsOf = [](const QString& text) {
        return text.split(QRegularExpression("[\\s()]+"), Qt::SkipEmptyParts);
    };

    const int lineCount = static_cast<int>(decompiledLines_.size());
    for (int i = 0; i < lineCount; ++i) {
        const QString text =
            QString::fromStdString(decompiledLines_[i].text).trimmed();
        if (text.isEmpty()) continue;

        const QStringList words = wordsOf(text);
        const int w = static_cast<int>(words.size());
        auto lw = [&](int idx) -> QString {
            return (idx < w) ? words[idx].toLower() : QString();
        };

        // 1) Signature line for an argument: `on name(arg1, arg2)`
        if (isArgument && i == 0 && lw(0) == QLatin1String("on")) {
            for (int j = 0; j < w; ++j) {
                if (lw(j) == lowerName) return i;
            }
        }
        // 2) `put ... <var>` — the variable is the last token.
        if (lw(0) == QLatin1String("put") && lw(w - 1) == lowerName) {
            return i;
        }
        // 3) `<var> = ...`
        if (lw(0) == lowerName && lw(1) == QLatin1String("=")) {
            return i;
        }
        // 4) `set <var> to ...`
        if (lw(0) == QLatin1String("set") && lw(1) == lowerName &&
            lw(2) == QLatin1String("to")) {
            return i;
        }
        // 5) `repeat with <var> ...`
        if (lw(0) == QLatin1String("repeat") && lw(1) == QLatin1String("with") &&
            lw(2) == lowerName) {
            return i;
        }
    }
    return -1;
}

void CodeViewPanel::jumpToDecompiledLine(int line) {
    if (line < 0 || line >= static_cast<int>(decompiledLines_.size())) {
        return;
    }
    tabWidget_->setCurrentWidget(decompiledView_);

    QTextDocument* doc = decompiledView_->document();
    QTextBlock block = doc->findBlockByLineNumber(line);
    if (block.isValid()) {
        QTextCursor cursor(block);
        cursor.select(QTextCursor::LineUnderCursor);
        decompiledView_->setTextCursor(cursor);
        decompiledView_->centerCursor();
    }

    // Flash the line.
    QTextCursor selCursor(block);
    selCursor.select(QTextCursor::LineUnderCursor);
    QTextEdit::ExtraSelection selection;
    selection.cursor = selCursor;
    selection.format.setBackground(kFlashBackground);
    decompiledView_->setExtraSelections({selection});
    flashTimer_->start();
}

void CodeViewPanel::clearFlash() {
    decompiledView_->setExtraSelections({});
}

void CodeViewPanel::rebuildDisplay() {
    // setPlainText() below replaces both documents, so drop the flash
    // selection (its cursors would otherwise reference stale positions).
    clearFlash();

    // Build bytecode text and per-row gutter indicators.
    {
        QString text;
        std::vector<BreakpointGutter::State> modes;
        modes.reserve(instructions_.size());
        for (const auto& instr : instructions_) {
            // -1 means "no instruction is current" and must never match a line.
            const bool isCurrent = currentInstructionOffset_ >= 0 &&
                                   instr.offset == currentInstructionOffset_;
            const bool hasBp = breakpointOffsets_.count(instr.offset) > 0;
            modes.push_back(hasBp ? BreakpointGutter::State::Breakpoint
                                  : (isCurrent ? BreakpointGutter::State::Current
                                               : BreakpointGutter::State::Blank));

            QString line;
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
            text += line + QStringLiteral("\n");
        }
        bytecodeView_->setPlainText(text);

        // Keep the gutter's scroll in step with the code across the rebuild.
        const int scroll = bytecodeView_->verticalScrollBar()->value();
        bytecodeGutter_->rebuild(static_cast<int>(instructions_.size()), modes);
        bytecodeGutter_->setScrollValue(scroll);
    }

    // Build decompiled text and per-row gutter indicators.
    {
        QString text;
        std::vector<BreakpointGutter::State> modes;
        modes.reserve(decompiledLines_.size());
        for (const auto& line : decompiledLines_) {
            // Structural lines carry a -1 offset sentinel; -1 also means "no
            // current instruction", so both must be guarded or every
            // structural line would light up as the current line.
            const bool isCurrent = currentInstructionOffset_ >= 0 &&
                                   line.bytecodeOffset == currentInstructionOffset_;
            const bool hasBp = breakpointOffsets_.count(line.bytecodeOffset) > 0;
            modes.push_back(hasBp ? BreakpointGutter::State::Breakpoint
                                  : (isCurrent ? BreakpointGutter::State::Current
                                               : BreakpointGutter::State::Blank));

            QString displayLine;
            displayLine += QString::fromStdString(line.text);
            text += displayLine + QStringLiteral("\n");
        }
        decompiledView_->setPlainText(text);

        const int scroll = decompiledView_->verticalScrollBar()->value();
        decompiledGutter_->rebuild(static_cast<int>(decompiledLines_.size()), modes);
        decompiledGutter_->setScrollValue(scroll);
    }
}

} // namespace libreshockwave::debugger
