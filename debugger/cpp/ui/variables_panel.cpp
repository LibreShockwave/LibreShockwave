#include "variables_panel.hpp"

#include <QHeaderView>

namespace libreshockwave::debugger {

VariablesPanel::VariablesPanel(QWidget* parent)
    : QTabWidget(parent) {

    locals_tree_ = new QTreeWidget(this);
    locals_tree_->setColumnCount(3);
    locals_tree_->setHeaderLabels({QStringLiteral("Name"),
                                  QStringLiteral("Value"),
                                  QStringLiteral("Type")});
    locals_tree_->header()->setStretchLastSection(true);
    locals_tree_->setRootIsDecorated(true);
    addTab(locals_tree_, QStringLiteral("Locals"));

    globals_tree_ = new QTreeWidget(this);
    globals_tree_->setColumnCount(3);
    globals_tree_->setHeaderLabels({QStringLiteral("Name"),
                                   QStringLiteral("Value"),
                                   QStringLiteral("Type")});
    globals_tree_->header()->setStretchLastSection(true);
    globals_tree_->setRootIsDecorated(true);
    addTab(globals_tree_, QStringLiteral("Globals"));

    properties_tree_ = new QTreeWidget(this);
    properties_tree_->setColumnCount(3);
    properties_tree_->setHeaderLabels({QStringLiteral("Name"),
                                      QStringLiteral("Value"),
                                      QStringLiteral("Type")});
    properties_tree_->header()->setStretchLastSection(true);
    properties_tree_->setRootIsDecorated(true);
    addTab(properties_tree_, QStringLiteral("Properties"));
}

void VariablesPanel::updateFromSnapshot(const SnapshotData& data) {
    populateTree(locals_tree_, data.locals);
    populateTree(globals_tree_, data.globals);
    populateTree(properties_tree_, data.properties);
}

void VariablesPanel::clearAll() {
    locals_tree_->clear();
    globals_tree_->clear();
    properties_tree_->clear();
}

void VariablesPanel::populateTree(
    QTreeWidget* tree,
    const std::vector<SnapshotData::VariableEntry>& entries) {
    tree->clear();
    for (const auto& entry : entries) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, QString::fromStdString(entry.name));
        item->setText(1, QString::fromStdString(entry.value));
        item->setText(2, QString::fromStdString(entry.type));
    }
    // Resize columns to content
    for (int col = 0; col < 3; ++col) {
        tree->resizeColumnToContents(col);
    }
}

} // namespace libreshockwave::debugger
