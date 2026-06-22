#pragma once

#include <QDialog>
#include <QMap>
#include <QString>

class QTableWidget;
class QWidget;

namespace libreshockwave::editor {

class ExternalParamsDialog final : public QDialog {
public:
    explicit ExternalParamsDialog(const QMap<QString, QString>& currentParams, QWidget* parent = nullptr);

    [[nodiscard]] QMap<QString, QString> params() const;

private:
    void addRow(const QString& key, const QString& value);
    void addEmptyRow();
    void removeSelectedRows();
    void loadHabboPreset();
    void stopEditing() const;

    QTableWidget* table_ = nullptr;
};

} // namespace libreshockwave::editor
