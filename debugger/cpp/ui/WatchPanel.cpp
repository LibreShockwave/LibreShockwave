#include "WatchPanel.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

namespace libreshockwave::debugger {

WatchPanel::WatchPanel(QWidget* parent)
    : QWidget(parent) {

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Input row
    auto* inputRow = new QHBoxLayout();
    inputField_ = new QLineEdit(this);
    inputField_->setPlaceholderText(QStringLiteral("Enter expression..."));
    inputField_->setFont(QFont(QStringLiteral("monospace"), 11));
    connect(inputField_, &QLineEdit::returnPressed, this, &WatchPanel::onAddClicked);
    inputRow->addWidget(inputField_);

    addButton_ = new QPushButton(QStringLiteral("+"), this);
    addButton_->setFixedWidth(30);
    connect(addButton_, &QPushButton::clicked, this, &WatchPanel::onAddClicked);
    inputRow->addWidget(addButton_);

    layout->addLayout(inputRow);

    // Watch list
    watchTree_ = new QTreeWidget(this);
    watchTree_->setColumnCount(3);
    watchTree_->setHeaderLabels({QStringLiteral("Expression"),
                                  QStringLiteral("Value"),
                                  QStringLiteral("")});
    watchTree_->header()->setStretchLastSection(false);
    watchTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    watchTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    watchTree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    watchTree_->setColumnWidth(2, 24);
    watchTree_->setRootIsDecorated(false);
    layout->addWidget(watchTree_);
}

void WatchPanel::updateFromSnapshot(const SnapshotData& data) {
    // Update existing rows with values from the snapshot
    for (const auto& wr : data.watchResults) {
        // Find the row with matching id
        for (int i = 0; i < watchTree_->topLevelItemCount(); ++i) {
            auto* item = watchTree_->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toString().toStdString() == wr.id) {
                if (!wr.error.empty()) {
                    item->setText(1, QStringLiteral("Error: %1")
                                         .arg(QString::fromStdString(wr.error)));
                    item->setForeground(1, QColor(200, 80, 80));
                } else {
                    item->setText(1, QString::fromStdString(wr.value));
                    item->setForeground(1, palette().color(QPalette::Text));
                }
                break;
            }
        }
    }
}

void WatchPanel::addWatchRow(const std::string& id, const std::string& expression) {
    auto* item = new QTreeWidgetItem(watchTree_);
    item->setText(0, QString::fromStdString(expression));
    item->setText(1, QStringLiteral("—"));
    item->setData(0, Qt::UserRole, QString::fromStdString(id));

    // Remove button
    auto* removeBtn = new QPushButton(QStringLiteral("×"), this);
    removeBtn->setFixedSize(20, 20);
    removeBtn->setStyleSheet(QStringLiteral("QPushButton{border:none;color:#d95555;font-weight:bold;}"));
    connect(removeBtn, &QPushButton::clicked, this, &WatchPanel::onRemoveClicked);
    watchTree_->setItemWidget(item, 2, removeBtn);
}

void WatchPanel::clearAll() {
    watchTree_->clear();
}

void WatchPanel::onAddClicked() {
    const auto expr = inputField_->text().trimmed();
    if (expr.isEmpty()) return;
    emit watchAdded(expr.toStdString());
    inputField_->clear();
}

void WatchPanel::onRemoveClicked() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (btn == nullptr) return;

    // Find the item containing this button
    for (int i = 0; i < watchTree_->topLevelItemCount(); ++i) {
        auto* item = watchTree_->topLevelItem(i);
        if (watchTree_->itemWidget(item, 2) == btn) {
            const auto id = item->data(0, Qt::UserRole).toString().toStdString();
            emit watchRemoved(id);
            delete item;
            break;
        }
    }
}

} // namespace libreshockwave::debugger
