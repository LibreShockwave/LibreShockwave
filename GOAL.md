# Plan: Breakpoint system for Code window — Qt6 widget gutter (fresh)

## Context (ignore prior gutter files)
- Code window is two read-only code views (Bytecode / Decompiled Lingo) inside `QTabWidget` in `CodeViewPanel`. Current code is plain `QPlainTextEdit` with syntax highlighting; breakpoints were previously text prefixes (`> ` / `●` / `▶`) inside the document.
- Requirement: **do not restore text glyphs**. Build a real widget gutter with Qt6 widgets — buttons, alignment, tables — so breakpoints are clickable widgets, not selectable text, and rows stay pixel-aligned with code lines.
- Breakpoint backend already exists (`BreakpointManager`, `DebugController::toggleBreakpoint`, `DebugStateBridge`, `DebuggerWindow` persistence via `QSettings` MD5 per movie). This plan is only the **Code window UI** for setting/clearing and showing breakpoints.

## Goals
- Per-line breakpoint toggle that works whether running or paused (WASM parity).
- Visual states: `Blank` / `Current ▶` amber / `Breakpoint ●` red (breakpoint wins), theme-aware via `QGuiApplication::palette().color(QPalette::Base)`.
- F9 dual-mode, Clear All, last-clicked tracking for running mode.
- Structural Lingo lines (`bytecodeOffset == -1`) never toggle.
- No asset-specific branches; scroll sync 1:1; no text selection pollution.

## Non-goals
- Conditional / hit-count / logpoints.
- Per-breakpoint enable/disable UI (model has `enabled`, UI is add/remove only).
- Breakpoint list panel.

## Architecture — Widgets, Buttons, Alignment, Tables

### Chosen approach: `QTableWidget` + `QToolButton` cells + `QHBoxLayout` per tab
This satisfies all keywords with minimal custom code:
- **Table** gives a native column with fixed width, row heights, and scroll bar to sync.
- **Button** gives a real clickable widget (hover, pressed, `clicked`, focus policy, tooltip) instead of painted text.
- **Alignment** via table column width + button fixed size + `Qt::AlignCenter`.
- **Layout** `QHBoxLayout{ gutterTable , codeView }` inside each tab `QWidget`.

Why not alternatives:
- `QScrollArea` + `QVBoxLayout` of `QPushButton`s — manual virtualization, more layout churn.
- `QListView` + delegate — paints, not buttons.
- `QFrame` + `QGridLayout` — same as table but table already handles row insert/delete and scroll.

### Component sketch (no prior files assumed)

**`ui/BreakpointGutter` (new, `QWidget`):**
- Owns `QTableWidget* table_` (1 column, N rows), `QPlainTextEdit* editor_` (for font/metrics, not parent).
- `enum class State { Blank, Current, Breakpoint }`
- `gutterWidth = QFontMetrics(editor->font()).horizontalAdvance("●") + 12`, `rowHeight = metrics.lineSpacing()`
- Table config: `verticalHeader()->setVisible(false)`, `horizontalHeader()->setVisible(false)`, `setShowGrid(false)`, `setFrameShape(QFrame::NoFrame)`, `setSelectionMode(NoSelection)`, `setEditTriggers(NoEdit)`, `setFixedWidth(gutterWidth)`, `setColumnWidth(0, gutterWidth)`, `setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)`, `setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded)`
- Per row: `auto* btn = new QToolButton; btn->setAutoRaise(true); btn->setFixedSize(gutterWidth, rowHeight); btn->setFocusPolicy(Qt::NoFocus); btn->setCursor(Qt::PointingHandCursor); btn->setToolTip("Toggle breakpoint");` `btn->setText(state==Current?"▶":state==Breakpoint?"●":"")` + `setStyleSheet("color: #...")` via `indicatorColor(state)` (dark vs light). `connect(btn, &QToolButton::clicked, [row]{ emit rowClicked(row); })`. Stored in `vector<QToolButton*> cells_`.
- Public: `rebuild(int rowCount, vector<State> states)` — shrink: `delete cellWidget` + `removeRow` + `pop_back`; grow: `insertRow` + `setRowHeight` + create button; then `setState` for all. `State stateAt(int)`, `setScrollValue(int)`, `resetScroll()`.
- Scroll sync (bidirectional): `connect(editor->verticalScrollBar(), valueChanged, table->verticalScrollBar(), setValue)` and reverse.
- Signals: `rowClicked(int row)`.

**`ui/CodeViewPanel` changes:**
- Per tab: `auto* tab = new QWidget; auto* lay = new QHBoxLayout(tab); lay->setContentsMargins(0,0,0,0); lay->setSpacing(2); auto* gutter = new BreakpointGutter(view, tab); lay->addWidget(gutter); lay->addWidget(view, 1); tabWidget->addTab(tab, "Bytecode"/"Decompiled")`
- State: `vector<InstructionData> instructions_`, `vector<DecompiledLineData> decompiledLines_`, `set<int> breakpointOffsets_`, `int currentOffset_`, `int lastClickedOffset_`, `int currentScriptId_`, `string currentHandlerName_`
- `onGutterRow(int row, bool isBytecode)` — guard `scriptId>0 && !handler.empty()`, `offset = isBytecode ? instructions_[row].offset : decompiledLines_[row].bytecodeOffset`, if `<0` return, `lastClickedOffset_=offset`, `emit breakpointToggled(scriptId, handler, offset)`.
- `rebuild()` — build **plain** text only: Bytecode `QString("%1  ").arg(offset,4) + opcode.leftJustified(16) + argStr.rightJustified(6) + (annotation?"  ; "+a:"")`; Decompiled `line.text`. Build `vector<State> states` (`hasBp ? Breakpoint : isCurrent ? Current : Blank`), `view->setPlainText(text)`, `gutter->rebuild(n, states)`, preserve scroll `view->verticalScrollBar()->value() -> gutter->setScrollValue`.
- `clear()` → `view->clear()` + `gutter->rebuild(0,{})`.
- `eventFilter` only for `QEvent::ContextMenu` (declaration menu); gutter handled via signal.

**Highlighters:**
- `GutterHighlighter` keeps only `static bool darkBase()` (palette check), remove any `applyMarker`/`markerColor` and `> `/`●` handling.
- `BytecodeHighlighter` regex becomes `^(\d{4,})  ([A-Za-z][A-Za-z0-9]*)\s+(-?\d+)(  ;.*)?` — offset col 0, opcode col 6, no prefix groups. Update capture indices.

**`DebuggerWindow` wiring (unchanged logic):**
- `setupMenuBar/ToolBar` F9 `toggleBreakpointAction_`, Clear All, `updateToolbarState` enabled iff player exists.
- `onToggleBreakpoint()` — if paused: `snapshot.offset` → `toggleBreakpoint`; else: `lastClickedOffset()` + handler check.
- `onBreakpointToggled` → `context->toggleBreakpoint(...)` (sets tracing true) → `refreshBreakpoints()+persistBreakpoints()`
- `refreshBreakpoints()` — filter `breakpointManager.getAll()` by `currentScriptId/handler` → `set<int> offsets` → `codeViewPanel->setBreakpointOffsets`
- `onPaused(SnapshotData)` → `setCurrentInstruction(offset)` + `loadHandlerCode`
- Persistence `QSettings "debugger/breakpoints/<md5(movieKey)>"` stringlist `scriptId\thandler\toffset\tenabled`.

## Tasks
1. Create `debugger/cpp/ui/BreakpointGutter.hpp/.cpp` as above (table + `QToolButton`, alignment centered, `AUTOMOC`). Include `BreakpointGutter.moc`.
2. Modify `CodeViewPanel.hpp/.cpp`: forward-declare `BreakpointGutter` inside `libreshockwave::debugger`, replace constructor tabs with `QHBoxLayout` + gutter, add `BreakpointGutter*` members, implement `onGutterRow`, `rebuild`, `clear`, remove gutter-width `QFontMetrics` click heuristic and `QMouseEvent` handling.
3. Update highlighters: strip marker API, fix `BytecodeHighlighter` regex/groups, remove `applyGutterMarker` call in `LingoHighlighter`.
4. Update `debugger/cpp/CMakeLists.txt`: add `ui/BreakpointGutter.cpp/.hpp` to `libreshockwave_debugger` and `libreshockwave_debugger_code_view_tests`, keep `AUTOMOC ON`, include `<QScrollBar>` in `CodeViewPanel.cpp`.
5. Update tests: `highlight_test.cpp` — Lingo block without prefix, Bytecode without `  `/`> ▶ ` (offset 0, opcode 6, `;` 30), drop `markerColor` asserts. `code_view_test.cpp` — include `BreakpointGutter`, assert `gutter->stateAt(row)`, click cols bar 5 / vTotal 11 / qux 5, last `ContextMenu` bar 5.
6. Build & verify.

## Verification
- `cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON && cmake --build cmake-build-debug -j4 && ctest --test-dir cmake-build-debug -R "highlight|code_view"` — passes. Full `ctest` 6 suites pass.
- Manual: click button gutter in both tabs running/paused → red `●` toggles, amber `▶` for current, `●` wins, hover/pressed feedback, scroll gutter+editor together, F9 running requires prior click else message, Clear All clears, persistence survives Stop/Play/restart, structural lines not togglable, text selection not polluted.

## Risks
- Many buttons: `QTableWidget` handles up to few k rows, rowHeight fixed to `lineSpacing`.
- Focus steal: `setFocusPolicy(Qt::NoFocus)` on buttons.
- Row lambda invalidation: capture `capturedRow` by value, recreate on rebuild.
- Enum `Qt::ScrollBarAlwaysOff` vs `QScrollBar::` — use `Qt::`.
- Forward declare inside namespace, include `<QScrollBar>` for `verticalScrollBar()->value()`.

## Files
- New: `debugger/cpp/ui/BreakpointGutter.hpp`, `BreakpointGutter.cpp`
- Modify: `debugger/cpp/ui/CodeViewPanel.*`, `ui/highlight/GutterHighlighter.*`, `BytecodeHighlighter.*`, `LingoHighlighter.cpp`, `debugger/cpp/highlight_test.cpp`, `code_view_test.cpp`, `debugger/cpp/CMakeLists.txt`
