#include "VariablesPanel.hpp"

#include <QHeaderView>

namespace libreshockwave::debugger {

VariablesPanel::VariablesPanel(QWidget* parent)
    : QTabWidget(parent) {

    localsTree_ = new QTreeWidget(this);
    localsTree_->setColumnCount(3);
    localsTree_->setHeaderLabels({QStringLiteral("Name"),
                                  QStringLiteral("Value"),
                                  QStringLiteral("Type")});
    localsTree_->header()->setStretchLastSection(true);
    localsTree_->setRootIsDecorated(true);
    addTab(localsTree_, QStringLiteral("Locals"));

    globalsTree_ = new QTreeWidget(this);
    globalsTree_->setColumnCount(3);
    globalsTree_->setHeaderLabels({QStringLiteral("Name"),
                                   QStringLiteral("Value"),
                                   QStringLiteral("Type")});
    globalsTree_->header()->setStretchLastSection(true);
    globalsTree_->setRootIsDecorated(true);
    addTab(globalsTree_, QStringLiteral("Globals"));

    propertiesTree_ = new QTreeWidget(this);
    propertiesTree_->setColumnCount(3);
    propertiesTree_->setHeaderLabels({QStringLiteral("Name"),
                                      QStringLiteral("Value"),
                                      QStringLiteral("Type")});
    propertiesTree_->header()->setStretchLastSection(true);
    propertiesTree_->setRootIsDecorated(true);
    addTab(propertiesTree_, QStringLiteral("Properties"));
}

void VariablesPanel::updateFromSnapshot(const SnapshotData& data) {
    populateTree(localsTree_, data.locals);
    populateTree(globalsTree_, data.globals);
    populateTree(propertiesTree_, data.properties);
}

void VariablesPanel::clearAll() {
    localsTree_->clear();
    globalsTree_->clear();
    propertiesTree_->clear();
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
