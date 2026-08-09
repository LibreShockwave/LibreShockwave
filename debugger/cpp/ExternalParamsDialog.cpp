#include "ExternalParamsDialog.hpp"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace libreshockwave::debugger {
namespace {

constexpr auto kDefaultPresetFileName = "debugger-defaults.ini";

/// Bootstrap values for the editable default connection preset.
constexpr std::pair<const char*, const char*> kDefaultPresets[] = {
    {"sw1", "client.allow.cross.domain=1;client.notify.cross.domain=0"},
    {"sw2", "connection.info.host=verysecret.classichabbo.com;connection.info.port=30000"},
    {"sw3", "connection.mus.host=verysecret.classichabbo.com;connection.mus.port=38101"},
    {"sw4", "site.url=https://images.classichabbo.com;url.prefix=https://images.classichabbo.com"},
    {"sw5", "client.reload.url=https://images.classichabbo.com/client/beta?x=reauthenticate;client.fatal.error.url=https://images.classichabbo.com/clientutils?key=error"},
    {"sw6", "client.connection.failed.url=https://images.classichabbo.com/clientutils?key=connection_failed;external.variables.txt=https://images.classichabbo.com/gamedata/external_variables.txt?"},
    {"sw7", "external.texts.txt=https://images.classichabbo.com/gamedata/external_texts.txt?"},
    {"sw8", "use.sso.ticket=1;sso.ticket="},
};

QMap<QString, QString> memoryDefaultPreset() {
    QMap<QString, QString> result;
    for (const auto& [key, value] : kDefaultPresets) {
        result.insert(QString::fromUtf8(key), QString::fromUtf8(value));
    }
    return result;
}

QString defaultPresetFilePath() {
    QSettings applicationSettings;
    const QFileInfo settingsFile(applicationSettings.fileName());

    // QSettings uses a registry path for its native Windows backend rather
    // than a filesystem path.  AppConfigLocation is the corresponding
    // filesystem location in that case.
    const QDir settingsDirectory = settingsFile.isAbsolute()
        ? settingsFile.absoluteDir()
        : QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    return settingsDirectory.filePath(QString::fromLatin1(kDefaultPresetFileName));
}

bool writeDefaultPresetFile(const QString& path,
                            const QMap<QString, QString>& values) {
    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() &&
        !QDir().mkpath(fileInfo.absolutePath())) {
        return false;
    }

    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QMap<QString, QString> storedDefaultPreset() {
    const QString path = defaultPresetFilePath();
    if (!QFileInfo::exists(path)) {
        const auto defaults = memoryDefaultPreset();
        writeDefaultPresetFile(path, defaults);
        return defaults;
    }

    QSettings settings(path, QSettings::IniFormat);
    QMap<QString, QString> result;
    for (const auto& key : settings.allKeys()) {
        if (!key.isEmpty()) {
            result.insert(key, settings.value(key).toString());
        }
    }
    if (!result.isEmpty() && settings.status() == QSettings::NoError) {
        return result;
    }

    // Keep the debugger usable if an existing defaults file is unreadable or
    // empty.  Do not overwrite it: the user may need to repair it manually.
    return memoryDefaultPreset();
}

QTableWidgetItem* editableItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    return item;
}

} // namespace

ExternalParamsDialog::ExternalParamsDialog(
    const QMap<QString, QString>& currentParams, QWidget* parent)
    : QDialog(parent) {

    setWindowTitle(QStringLiteral("External Parameters"));
    setModal(true);
    setMinimumSize(560, 320);

    auto* layout = new QVBoxLayout(this);

    auto* help = new QLabel(
        QStringLiteral("Set key=value pairs passed to the movie at startup.\n"
                       "These are typically needed for network-dependent movies.\n"
                       "Leave empty if the movie doesn't need external parameters."),
        this);
    help->setWordWrap(true);
    layout->addWidget(help);

    // Table
    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("Key"), QStringLiteral("Value")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setProperty("terminateEditOnFocusLost", true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->resizeSection(0, 140);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    // Populate with existing params
    for (auto it = currentParams.cbegin(); it != currentParams.cend(); ++it) {
        addRow(it.key(), it.value());
    }

    // If empty, add one blank row so the user can start typing immediately
    if (table_->rowCount() == 0) {
        addRow(QString(), QString());
    }

    // Bottom buttons
    auto* bottom = new QHBoxLayout;

    auto* addBtn = new QPushButton(QStringLiteral("&Add Row"), this);
    auto* removeBtn = new QPushButton(QStringLiteral("&Remove Selected"), this);
    auto* presetBtn = new QPushButton(QStringLiteral("&Default Preset"), this);
    presetBtn->setToolTip(QStringLiteral("Fill in the default connection parameters"));

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    bottom->addWidget(addBtn);
    bottom->addWidget(removeBtn);
    bottom->addSpacing(16);
    bottom->addWidget(presetBtn);
    bottom->addStretch();
    bottom->addWidget(buttonBox);
    layout->addLayout(bottom);

    connect(addBtn, &QPushButton::clicked, this, [this] { addRow(); });
    connect(removeBtn, &QPushButton::clicked, this, [this] { removeSelectedRows(); });
    connect(presetBtn, &QPushButton::clicked, this, [this] { loadDefaultPreset(); });
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this] {
        stopEditing();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExternalParamsDialog::ensureDefaultPresetFile() {
    const QString path = defaultPresetFilePath();
    if (!QFileInfo::exists(path)) {
        writeDefaultPresetFile(path, memoryDefaultPreset());
    }
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

std::vector<std::pair<std::string, std::string>> ExternalParamsDialog::toPlayerParams(
    const QMap<QString, QString>& params) {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(params.size());
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        result.emplace_back(it.key().toStdString(), it.value().toStdString());
    }
    return result;
}

void ExternalParamsDialog::addRow() {
    addRow(QString(), QString());
    const int row = table_->rowCount() - 1;
    table_->setCurrentCell(row, 0);
    table_->editItem(table_->item(row, 0));
}

void ExternalParamsDialog::addRow(const QString& key, const QString& value) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, editableItem(key));
    table_->setItem(row, 1, editableItem(value));
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

void ExternalParamsDialog::loadDefaultPreset() {
    stopEditing();
    table_->setRowCount(0);
    const auto defaults = storedDefaultPreset();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        addRow(it.key(), it.value());
    }
}

void ExternalParamsDialog::stopEditing() const {
    if (auto* current = table_->currentItem()) {
        table_->closePersistentEditor(current);
    }
    if (auto* focus = QApplication::focusWidget();
        focus && (focus == table_ || table_->isAncestorOf(focus))) {
        focus->clearFocus();
    }
}

} // namespace libreshockwave::debugger
