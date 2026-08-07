#pragma once

#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>

#include "model/DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Panel for managing watch expressions and viewing their evaluated results.
class WatchPanel : public QWidget {
    Q_OBJECT

public:
    explicit WatchPanel(QWidget* parent = nullptr);

    /// Update watch results from a pause snapshot.
    void updateFromSnapshot(const SnapshotData& data);

    /// Add a watch expression row with a pending ("—") value.
    void addWatchRow(const std::string& id, const std::string& expression);

    /// Clear all watch rows.
    void clearAll();

signals:
    /// Emitted when the user adds a new watch expression.
    void watchAdded(const std::string& expression);
    /// Emitted when the user removes a watch expression.
    void watchRemoved(const std::string& id);

private slots:
    void onAddClicked();
    void onRemoveClicked();

private:
    QLineEdit* inputField_;
    QPushButton* addButton_;
    QTreeWidget* watchTree_;
};

} // namespace libreshockwave::debugger
