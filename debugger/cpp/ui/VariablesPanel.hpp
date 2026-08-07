#pragma once

#include <QTabWidget>
#include <QTreeWidget>

#include "model/DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Panel with tabs for Locals, Globals, and Properties inspection.
class VariablesPanel : public QTabWidget {
    Q_OBJECT

public:
    explicit VariablesPanel(QWidget* parent = nullptr);

    /// Update all variable views from a pause snapshot.
    void updateFromSnapshot(const SnapshotData& data);

    /// Clear all variable views (when execution resumes).
    void clearAll();

private:
    void populateTree(QTreeWidget* tree,
                      const std::vector<SnapshotData::VariableEntry>& entries);

    QTreeWidget* localsTree_;
    QTreeWidget* globalsTree_;
    QTreeWidget* propertiesTree_;
};

} // namespace libreshockwave::debugger
