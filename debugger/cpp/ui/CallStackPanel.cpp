#include "CallStackPanel.hpp"

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
    setRowCount(static_cast<int>(data.callStack.size()));
    for (int i = 0; i < static_cast<int>(data.callStack.size()); ++i) {
        // Reverse index: innermost frame at row 0
        const auto& frame = data.callStack[data.callStack.size() - 1 - i];
        auto* handlerItem = new QTableWidgetItem(QString::fromStdString(frame.handlerName));
        auto* scriptItem = new QTableWidgetItem(QString::fromStdString(frame.scriptName));
        setItem(i, 0, handlerItem);
        setItem(i, 1, scriptItem);
    }
    resizeColumnToContents(0);
}

void CallStackPanel::clearAll() {
    setRowCount(0);
}

} // namespace libreshockwave::debugger
