#pragma once

#include <QTabWidget>
#include <QTreeWidget>

#include "model/debugger_model.hpp"

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

    QTreeWidget* locals_tree_;
    QTreeWidget* globals_tree_;
    QTreeWidget* properties_tree_;
};

} // namespace libreshockwave::debugger
