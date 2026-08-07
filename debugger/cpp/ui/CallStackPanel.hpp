#pragma once

#include <QTableWidget>

#include "model/DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Panel showing the call stack when execution is paused.
class CallStackPanel : public QTableWidget {
    Q_OBJECT

public:
    explicit CallStackPanel(QWidget* parent = nullptr);

    /// Update from a pause snapshot's call stack.
    void updateFromSnapshot(const SnapshotData& data);

    /// Clear the call stack (when execution resumes).
    void clearAll();
};

} // namespace libreshockwave::debugger
