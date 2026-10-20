#include "watch_panel.hpp"

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
    auto* input_row = new QHBoxLayout();
    input_field_ = new QLineEdit(this);
    input_field_->setPlaceholderText(QStringLiteral("Enter expression..."));
    input_field_->setFont(QFont(QStringLiteral("monospace"), 11));
    connect(input_field_, &QLineEdit::returnPressed, this, &WatchPanel::onAddClicked);
    input_row->addWidget(input_field_);

    add_button_ = new QPushButton(QStringLiteral("+"), this);
    add_button_->setFixedWidth(30);
    connect(add_button_, &QPushButton::clicked, this, &WatchPanel::onAddClicked);
    input_row->addWidget(add_button_);

    layout->addLayout(input_row);

    // Watch list
    watch_tree_ = new QTreeWidget(this);
    watch_tree_->setColumnCount(3);
    watch_tree_->setHeaderLabels({QStringLiteral("Expression"),
                                  QStringLiteral("Value"),
                                  QStringLiteral("")});
    watch_tree_->header()->setStretchLastSection(false);
    watch_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    watch_tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    watch_tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    watch_tree_->setColumnWidth(2, 24);
    watch_tree_->setRootIsDecorated(false);
    layout->addWidget(watch_tree_);
}

void WatchPanel::updateFromSnapshot(const SnapshotData& data) {
    // Update existing rows with values from the snapshot
    for (const auto& wr : data.watch_results) {
        // Find the row with matching id
        for (int i = 0; i < watch_tree_->topLevelItemCount(); ++i) {
            auto* item = watch_tree_->topLevelItem(i);
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
    auto* item = new QTreeWidgetItem(watch_tree_);
    item->setText(0, QString::fromStdString(expression));
    item->setText(1, QStringLiteral("—"));
    item->setData(0, Qt::UserRole, QString::fromStdString(id));

    // Remove button
    auto* remove_btn = new QPushButton(QStringLiteral("×"), this);
    remove_btn->setFixedSize(20, 20);
    remove_btn->setStyleSheet(QStringLiteral("QPushButton{border:none;color:#d95555;font-weight:bold;}"));
    connect(remove_btn, &QPushButton::clicked, this, &WatchPanel::onRemoveClicked);
    watch_tree_->setItemWidget(item, 2, remove_btn);
}

void WatchPanel::clearAll() {
    watch_tree_->clear();
}

void WatchPanel::onAddClicked() {
    const auto expr = input_field_->text().trimmed();
    if (expr.isEmpty()) return;
    emit watchAdded(expr.toStdString());
    input_field_->clear();
}

void WatchPanel::onRemoveClicked() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (btn == nullptr) return;

    // Find the item containing this button
    for (int i = 0; i < watch_tree_->topLevelItemCount(); ++i) {
        auto* item = watch_tree_->topLevelItem(i);
        if (watch_tree_->itemWidget(item, 2) == btn) {
            const auto id = item->data(0, Qt::UserRole).toString().toStdString();
            emit watchRemoved(id);
            delete item;
            break;
        }
    }
}

} // namespace libreshockwave::debugger
