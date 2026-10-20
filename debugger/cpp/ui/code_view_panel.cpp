#include "code_view_panel.hpp"

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

#include "ui/breakpoint_gutter.hpp"
#include "ui/highlight/bytecode_highlighter.hpp"
#include "ui/highlight/lingo_highlighter.hpp"

namespace libreshockwave::debugger {

namespace {

// Flash shown after a go-to-declaration jump fades out after this long.
constexpr int k_flash_duration_ms = 1500;

// Amber flash background (semi-transparent) for the jumped-to line.
const QColor kFlashBackground(0xff, 0xd5, 0x4f, 0x5c);

} // namespace

CodeViewPanel::CodeViewPanel(QWidget* parent)
    : QWidget(parent) {

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top bar: handler dropdown + info label
    auto* top_bar = new QHBoxLayout();
    top_bar->setContentsMargins(4, 2, 4, 2);

    handler_combo_ = new QComboBox(this);
    handler_combo_->setMinimumWidth(200);
    handler_combo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(handler_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CodeViewPanel::onHandlerComboChanged);
    top_bar->addWidget(handler_combo_);

    info_label_ = new QLabel(this);
    top_bar->addWidget(info_label_);
    top_bar->addStretch();

    layout->addLayout(top_bar);

    // Tab widget: bytecode / decompiled.  Each tab pairs its code view with a
    // gutter of per-line indicator widgets to its left.
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setTabPosition(QTabWidget::North);

    bytecode_view_ = new QPlainTextEdit(this);
    bytecode_view_->setReadOnly(true);
    bytecode_view_->setFont(QFont(QStringLiteral("monospace"), 11));
    bytecode_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    bytecode_view_->setTabStopDistance(32);
    bytecode_highlighter_ = new BytecodeHighlighter(bytecode_view_->document());
    {
        auto* bytecode_tab = new QWidget(this);
        bytecode_tab_ = bytecode_tab;
        auto* bytecode_layout = new QHBoxLayout(bytecode_tab);
        bytecode_layout->setContentsMargins(0, 0, 0, 0);
        bytecode_gutter_ = new BreakpointGutter(bytecode_view_, bytecode_tab);
        connect(bytecode_gutter_, &BreakpointGutter::rowClicked, this,
                [this](int row) { handleGutterRow(row, true); });
        bytecode_layout->addWidget(bytecode_gutter_);
        bytecode_layout->addWidget(bytecode_view_, 1);
        tab_widget_->addTab(bytecode_tab, QStringLiteral("Bytecode"));
    }

    decompiled_view_ = new QPlainTextEdit(this);
    decompiled_view_->setReadOnly(true);
    decompiled_view_->setFont(QFont(QStringLiteral("monospace"), 11));
    decompiled_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    lingo_highlighter_ = new LingoHighlighter(decompiled_view_->document());
    {
        auto* decompiled_tab = new QWidget(this);
        decompiled_tab_ = decompiled_tab;
        auto* decompiled_layout = new QHBoxLayout(decompiled_tab);
        decompiled_layout->setContentsMargins(0, 0, 0, 0);
        decompiled_gutter_ = new BreakpointGutter(decompiled_view_, decompiled_tab);
        connect(decompiled_gutter_, &BreakpointGutter::rowClicked, this,
                [this](int row) { handleGutterRow(row, false); });
        decompiled_layout->addWidget(decompiled_gutter_);
        decompiled_layout->addWidget(decompiled_view_, 1);
        tab_widget_->addTab(decompiled_tab, QStringLiteral("Decompiled"));
    }

    flash_timer_ = new QTimer(this);
    flash_timer_->setSingleShot(true);
    flash_timer_->setInterval(k_flash_duration_ms);
    connect(flash_timer_, &QTimer::timeout, this, &CodeViewPanel::clearFlash);

    layout->addWidget(tab_widget_);

    connect(tab_widget_, &QTabWidget::currentChanged, this, &CodeViewPanel::onTabChanged);

    // Click-to-toggle breakpoints in the gutter column of either view.
    // The event filter is installed on the viewports (the widgets that
    // actually receive mouse events) so cursorForPosition() gets
    // viewport-relative coordinates.
    bytecode_view_->viewport()->installEventFilter(this);
    decompiled_view_->viewport()->installEventFilter(this);
}

void CodeViewPanel::setHandlerCode(
    int cast_lib_number,
    int script_id,
    const std::string& script_name,
    const std::string& handler_name,
    const std::vector<InstructionData>& instructions,
    const std::vector<DecompiledLineData>& decompiled_lines,
    const std::vector<std::string>& handler_names,
    const std::vector<std::string>& arg_names,
    const std::vector<std::string>& local_names) {

    current_cast_lib_number_ = cast_lib_number;
    current_script_id_ = script_id;
    script_name_ = script_name;
    current_handler_name_ = handler_name;
    instructions_ = instructions;
    decompiled_lines_ = decompiled_lines;
    handler_names_ = handler_names;
    last_clicked_offset_ = -1;

    arg_names_.clear();
    local_names_.clear();
    for (const auto& name : arg_names) {
        arg_names_.insert(QString::fromStdString(name).toLower());
    }
    for (const auto& name : local_names) {
        local_names_.insert(QString::fromStdString(name).toLower());
    }
    QSet<QString> variables = arg_names_;
    variables.unite(local_names_);
    lingo_highlighter_->setVariableNames(variables);

    // Update handler combo
    handler_combo_->blockSignals(true);
    handler_combo_->clear();
    handler_combo_->addItem(QStringLiteral("— Overview —"));
    for (const auto& h : handler_names) {
        handler_combo_->addItem(QString::fromStdString(h));
    }
    // Select the current handler
    if (!handler_name.empty()) {
        const int idx = handler_combo_->findText(QString::fromStdString(handler_name));
        if (idx >= 0) handler_combo_->setCurrentIndex(idx);
    }
    handler_combo_->blockSignals(false);

    // Update info label
    info_label_->setText(
        QStringLiteral("%1 → %2").arg(
            QString::fromStdString(script_name),
            handler_name.empty() ? QStringLiteral("Overview")
                                : QString::fromStdString(handler_name)));

    rebuildDisplay();
}

void CodeViewPanel::setCurrentInstruction(int offset) {
    current_instruction_offset_ = offset;
    rebuildDisplay();
}

void CodeViewPanel::setBreakpointOffsets(const std::set<int>& offsets) {
    breakpoint_offsets_ = offsets;
    rebuildDisplay();
}

void CodeViewPanel::setSymbolIndex(const SymbolIndex& index) {
    symbol_index_ = index;
    QSet<QString> methods;
    QSet<QString> declarations;
    for (const auto& name : index.methodNames()) {
        methods.insert(QString::fromStdString(name));
    }
    for (const auto& name : index.declarationNames()) {
        declarations.insert(QString::fromStdString(name));
    }
    lingo_highlighter_->setMethodNames(methods);
    lingo_highlighter_->setDeclarationNames(declarations);
    bytecode_highlighter_->setDeclarationNames(declarations);
}

void CodeViewPanel::clear() {
    current_cast_lib_number_ = 0;
    current_script_id_ = 0;
    script_name_.clear();
    current_handler_name_.clear();
    instructions_.clear();
    decompiled_lines_.clear();
    handler_names_.clear();
    arg_names_.clear();
    local_names_.clear();
    symbol_index_ = SymbolIndex();
    breakpoint_offsets_.clear();
    current_instruction_offset_ = -1;
    last_clicked_offset_ = -1;
    handler_combo_->clear();
    info_label_->clear();
    lingo_highlighter_->setMethodNames({});
    lingo_highlighter_->setDeclarationNames({});
    lingo_highlighter_->setVariableNames({});
    bytecode_highlighter_->setDeclarationNames({});
    clearFlash();

    bytecode_view_->clear();
    decompiled_view_->clear();
    bytecode_gutter_->rebuild(0, {});
    decompiled_gutter_->rebuild(0, {});
}

bool CodeViewPanel::eventFilter(QObject* obj, QEvent* event) {
    // Qt's default text menu (Copy / Select All) is shown for a QEvent::ContextMenu,
    // a separate event from the right-button release.  Replace it for either code
    // view with the unified menu (standard actions + "Go to declaration" options).
    if (event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        if (obj == bytecode_view_->viewport() &&
            handleRightClick(bytecode_view_, ce->pos())) {
            event->accept();
            return true;
        }
        if (obj == decompiled_view_->viewport() &&
            handleRightClick(decompiled_view_, ce->pos())) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CodeViewPanel::handleGutterRow(int row, bool is_bytecode) {
    // A handler must be displayed to toggle against (matches the WASM
    // harness, which uses the currently selected handler).
    if (current_script_id_ <= 0 || current_handler_name_.empty()) {
        return;
    }

    int offset = -1;
    if (is_bytecode) {
        if (row >= 0 && row < static_cast<int>(instructions_.size())) {
            offset = instructions_[row].offset;
        }
    } else {
        if (row >= 0 && row < static_cast<int>(decompiled_lines_.size())) {
            offset = decompiled_lines_[row].bytecode_offset;
        }
    }
    if (offset < 0) {
        return;
    }

    last_clicked_offset_ = offset;
    emit breakpointToggled(current_script_id_, current_handler_name_, offset);
}

void CodeViewPanel::onTabChanged(int /*index*/) {
    rebuildDisplay();
    revealLastClickedOffset();
}

void CodeViewPanel::onHandlerComboChanged(int index) {
    if (index < 0) return;
    const auto name = handler_combo_->itemText(index).toStdString();
    if (name != current_handler_name_) {
        emit handlerChanged(name);
    }
}

bool CodeViewPanel::handleRightClick(QPlainTextEdit* view, const QPoint& viewport_pos) {
    QMenu* menu = buildRightClickMenu(view, viewport_pos);
    // WA_DeleteOnClose: the menu deletes itself once exec() returns.
    menu->exec(view->viewport()->mapToGlobal(viewport_pos));
    return true;
}

QMenu* CodeViewPanel::buildRightClickMenu(QPlainTextEdit* view,
                                          const QPoint& viewport_pos) {
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
    QTextCursor cursor = view->cursorForPosition(viewport_pos);
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
    if (view == decompiled_view_) {
        // Local variables and arguments declared in the current handler
        // jump to their first assignment in this handler's decompiled code.
        const QString lower = word.toLower();
        if (arg_names_.contains(lower) || local_names_.contains(lower)) {
            const bool is_argument = arg_names_.contains(lower);
            const int line = findVariableDeclarationLine(lower, is_argument);
            if (line >= 0) {
                const int target_line = line;
                const auto* action = menu->addAction(
                    QStringLiteral("Go to %1 declaration (line %2)")
                        .arg(is_argument ? QStringLiteral("argument")
                                        : QStringLiteral("variable"),
                             QString::number(line + 1)));
                connect(action, &QAction::triggered, this,
                        [this, target_line]() {
                            revealLine(decompiled_view_, target_line);
                        });
            }
        }
    }

    // Methods, properties, and globals resolve across the whole movie.
    if (!symbol_index_.empty()) {
        const auto targets = symbol_index_.find(word.toStdString(), current_script_id_);
        for (const auto& target : targets) {
            const QString label =
                target.kind == DeclarationKind::Method
                    ? QStringLiteral("Go to method")
                    : (target.kind == DeclarationKind::Property
                           ? QStringLiteral("Go to property")
                           : QStringLiteral("Go to global"));
            const bool is_current =
                target.kind == DeclarationKind::Method &&
                target.cast_lib_number == current_cast_lib_number_ &&
                target.script_id == current_script_id_ &&
                target.handler_name == current_handler_name_;
            if (is_current) {
                menu->addAction(
                        QStringLiteral("%1 (%2) — current")
                            .arg(label, QString::fromStdString(target.script_name)))
                    ->setEnabled(false);
                continue;
            }
            const DeclarationTarget target_copy = target;
            menu->addAction(
                QStringLiteral("%1 (%2)").arg(
                    label, QString::fromStdString(target.script_name)));
            connect(menu->actions().last(), &QAction::triggered, this,
                    [this, target_copy]() {
                        emit goToDeclarationRequested(target_copy);
                    });
        }
    }
}

int CodeViewPanel::findVariableDeclarationLine(const QString& lower_name,
                                               bool is_argument) const {
    const auto words_of = [](const QString& text) {
        return text.split(QRegularExpression("[\\s()]+"), Qt::SkipEmptyParts);
    };

    const int line_count = static_cast<int>(decompiled_lines_.size());
    for (int i = 0; i < line_count; ++i) {
        const QString text =
            QString::fromStdString(decompiled_lines_[i].text).trimmed();
        if (text.isEmpty()) continue;

        const QStringList words = words_of(text);
        const int w = static_cast<int>(words.size());
        auto lw = [&](int idx) -> QString {
            return (idx < w) ? words[idx].toLower() : QString();
        };

        // 1) Signature line for an argument: `on name(arg1, arg2)`
        if (is_argument && i == 0 && lw(0) == QLatin1String("on")) {
            for (int j = 0; j < w; ++j) {
                if (lw(j) == lower_name) return i;
            }
        }
        // 2) `put ... <var>` — the variable is the last token.
        if (lw(0) == QLatin1String("put") && lw(w - 1) == lower_name) {
            return i;
        }
        // 3) `<var> = ...`
        if (lw(0) == lower_name && lw(1) == QLatin1String("=")) {
            return i;
        }
        // 4) `set <var> to ...`
        if (lw(0) == QLatin1String("set") && lw(1) == lower_name &&
            lw(2) == QLatin1String("to")) {
            return i;
        }
        // 5) `repeat with <var> ...`
        if (lw(0) == QLatin1String("repeat") && lw(1) == QLatin1String("with") &&
            lw(2) == lower_name) {
            return i;
        }
    }
    return -1;
}

void CodeViewPanel::revealLine(QPlainTextEdit* view, int line) {
    QTextDocument* doc = view->document();
    QTextBlock block = doc->findBlockByLineNumber(line);
    if (!block.isValid()) {
        return;
    }
    view->setTextCursor(QTextCursor(block));
    view->centerCursor();

    // Flash the line.
    QTextCursor sel_cursor(block);
    sel_cursor.select(QTextCursor::LineUnderCursor);
    QTextEdit::ExtraSelection selection;
    selection.cursor = sel_cursor;
    selection.format.setBackground(kFlashBackground);
    view->setExtraSelections({selection});
    flash_timer_->start();
}

void CodeViewPanel::revealLastClickedOffset() {
    if (last_clicked_offset_ < 0) {
        return;
    }
    if (tab_widget_->currentWidget() == bytecode_tab_) {
        const int row = bytecodeRowForOffset(last_clicked_offset_);
        if (row >= 0) {
            revealLine(bytecode_view_, row);
        }
    } else {
        const int row = decompiledRowForOffset(last_clicked_offset_);
        if (row >= 0) {
            revealLine(decompiled_view_, row);
        }
    }
}

int CodeViewPanel::bytecodeRowForOffset(int offset) const {
    for (int i = 0; i < static_cast<int>(instructions_.size()); ++i) {
        if (instructions_[i].offset == offset) {
            return i;
        }
    }
    return -1;
}

int CodeViewPanel::decompiledRowForOffset(int offset) const {
    int best = -1;
    for (int i = 0; i < static_cast<int>(decompiled_lines_.size()); ++i) {
        const int start = decompiled_lines_[i].bytecode_offset;
        if (start >= 0 && start <= offset) {
            best = i;
        }
    }
    return best;
}

void CodeViewPanel::clearFlash() {
    bytecode_view_->setExtraSelections({});
    decompiled_view_->setExtraSelections({});
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
            const bool is_current = current_instruction_offset_ >= 0 &&
                                   instr.offset == current_instruction_offset_;
            const bool has_bp = breakpoint_offsets_.count(instr.offset) > 0;
            modes.push_back(has_bp ? BreakpointGutter::State::Breakpoint
                                  : (is_current ? BreakpointGutter::State::Current
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
        bytecode_view_->setPlainText(text);

        // Keep the gutter's scroll in step with the code across the rebuild.
        const int scroll = bytecode_view_->verticalScrollBar()->value();
        bytecode_gutter_->rebuild(static_cast<int>(instructions_.size()), modes);
        bytecode_gutter_->setScrollValue(scroll);
    }

    // Build decompiled text and per-row gutter indicators.
    {
        QString text;
        std::vector<BreakpointGutter::State> modes;
        modes.reserve(decompiled_lines_.size());
        for (const auto& line : decompiled_lines_) {
            // Structural lines carry a -1 offset sentinel; -1 also means "no
            // current instruction", so both must be guarded or every
            // structural line would light up as the current line.
            const bool is_current = current_instruction_offset_ >= 0 &&
                                   line.bytecode_offset == current_instruction_offset_;
            const bool has_bp = breakpoint_offsets_.count(line.bytecode_offset) > 0;
            modes.push_back(has_bp ? BreakpointGutter::State::Breakpoint
                                  : (is_current ? BreakpointGutter::State::Current
                                               : BreakpointGutter::State::Blank));

            QString display_line;
            display_line += QString::fromStdString(line.text);
            text += display_line + QStringLiteral("\n");
        }
        decompiled_view_->setPlainText(text);

        const int scroll = decompiled_view_->verticalScrollBar()->value();
        decompiled_gutter_->rebuild(static_cast<int>(decompiled_lines_.size()), modes);
        decompiled_gutter_->setScrollValue(scroll);
    }
}

} // namespace libreshockwave::debugger
