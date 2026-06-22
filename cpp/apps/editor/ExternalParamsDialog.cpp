#include "ExternalParamsDialog.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "EditorModels.hpp"

namespace libreshockwave::editor {
namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QTableWidgetItem* editableItem(const QString& text) {
    return new QTableWidgetItem(text);
}

} // namespace

ExternalParamsDialog::ExternalParamsDialog(const QMap<QString, QString>& currentParams, QWidget* parent)
    : QDialog(parent) {
    const auto text = externalParamsDialogText();

    setWindowTitle(toQString(text.windowTitle));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(toQString(text.helpText), this);
    layout->addWidget(help);

    table_ = new QTableWidget(0, 2, this);
    QStringList tableColumns;
    for (const auto column : externalParamsTableColumns()) {
        tableColumns.push_back(toQString(column));
    }
    table_->setHorizontalHeaderLabels(tableColumns);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setMinimumSize(560, 200);
    table_->setProperty("terminateEditOnFocusLost", true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->resizeSection(0, 120);
    table_->horizontalHeader()->resizeSection(1, 400);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    for (auto it = currentParams.cbegin(); it != currentParams.cend(); ++it) {
        addRow(it.key(), it.value());
    }

    auto* bottom = new QHBoxLayout;
    auto* add = new QPushButton(toQString(text.addText), this);
    add->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
    auto* remove = new QPushButton(toQString(text.removeText), this);
    remove->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    auto* habboPreset = new QPushButton(toQString(text.habboPresetText), this);
    habboPreset->setToolTip(toQString(text.habboPresetTooltip));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    bottom->addWidget(add);
    bottom->addWidget(remove);
    bottom->addSpacing(20);
    bottom->addWidget(habboPreset);
    bottom->addStretch(1);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    connect(add, &QPushButton::clicked, this, [this] { addEmptyRow(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeSelectedRows(); });
    connect(habboPreset, &QPushButton::clicked, this, [this] { loadHabboPreset(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        stopEditing();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QMap<QString, QString> ExternalParamsDialog::params() const {
    stopEditing();

    QMap<QString, QString> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* keyItem = table_->item(row, 0);
        const auto* valueItem = table_->item(row, 1);
        const QString key = keyItem ? keyItem->text().trimmed() : QString();
        if (!key.isEmpty()) {
            result.insert(key, valueItem ? valueItem->text() : QString());
        }
    }
    return result;
}

void ExternalParamsDialog::addRow(const QString& key, const QString& value) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, editableItem(key));
    table_->setItem(row, 1, editableItem(value));
}

void ExternalParamsDialog::addEmptyRow() {
    stopEditing();
    addRow({}, {});
    const int row = table_->rowCount() - 1;
    table_->setCurrentCell(row, 0);
    table_->editItem(table_->item(row, 0));
}

void ExternalParamsDialog::removeSelectedRows() {
    stopEditing();

    QList<int> rows;
    for (const auto* item : table_->selectedItems()) {
        if (!rows.contains(item->row())) {
            rows.push_back(item->row());
        }
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (const int row : rows) {
        table_->removeRow(row);
    }
}

void ExternalParamsDialog::loadHabboPreset() {
    stopEditing();
    table_->setRowCount(0);
    for (const auto& param : habboExternalParamPreset()) {
        addRow(QString::fromStdString(param.key), QString::fromStdString(param.value));
    }
}

void ExternalParamsDialog::stopEditing() const {
    if (auto* current = table_->currentItem()) {
        table_->closePersistentEditor(table_->currentItem());
    }
    if (auto* focus = QApplication::focusWidget(); focus && (focus == table_ || table_->isAncestorOf(focus))) {
        focus->clearFocus();
    }
}

} // namespace libreshockwave::editor
