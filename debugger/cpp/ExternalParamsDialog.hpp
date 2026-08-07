#pragma once

#include <QDialog>
#include <QMap>
#include <QString>
#include <vector>
#include <string>
#include <utility>

class QTableWidget;

namespace libreshockwave::debugger {

/// Dialog for editing external movie parameters (key=value pairs) before
/// loading a Director movie.  Includes a preset for ClassicHabbo connections.
class ExternalParamsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExternalParamsDialog(const QMap<QString, QString>& currentParams,
                                  QWidget* parent = nullptr);

    /// Return the current key=value pairs from the table.
    [[nodiscard]] QMap<QString, QString> params() const;

    /// Convert QMap to the vector<pair> format Player expects.
    [[nodiscard]] static std::vector<std::pair<std::string, std::string>> toPlayerParams(
        const QMap<QString, QString>& params);

private slots:
    void addRow();
    void removeSelectedRows();

public:
    /// Fill the table with the default connection preset.
    void loadDefaultPreset();

private:
    void addRow(const QString& key, const QString& value);
    void stopEditing() const;

    QTableWidget* table_ = nullptr;
};

} // namespace libreshockwave::debugger
