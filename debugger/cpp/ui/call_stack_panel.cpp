#include "call_stack_panel.hpp"

#include <QHeaderView>

namespace libreshockwave::debugger {

CallStackPanel::CallStackPanel(QWidget* parent)
    : QTableWidget(0, 2, parent) {
    setHorizontalHeaderLabels({QStringLiteral("Handler"),
                               QStringLiteral("Script")});
    horizontalHeader()->setStretchLastSection(true);
    verticalHeader()->setVisible(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    setAlternatingRowColors(true);
}

void CallStackPanel::updateFromSnapshot(const SnapshotData& data) {
    clearAll();

    // Show innermost frame first (reverse of the stored order, matching the
    // WASM harness behavior).
    setRowCount(static_cast<int>(data.call_stack.size()));
    for (int i = 0; i < static_cast<int>(data.call_stack.size()); ++i) {
        // Reverse index: innermost frame at row 0
        const auto& frame = data.call_stack[data.call_stack.size() - 1 - i];
        auto* handler_item = new QTableWidgetItem(QString::fromStdString(frame.handler_name));
        auto* script_item = new QTableWidgetItem(QString::fromStdString(frame.script_name));
        setItem(i, 0, handler_item);
        setItem(i, 1, script_item);
    }
    resizeColumnToContents(0);
}

void CallStackPanel::clearAll() {
    setRowCount(0);
}

} // namespace libreshockwave::debugger
