#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QWidget;

namespace libreshockwave::editor {

class StartScreenDialog final : public QDialog {
public:
    explicit StartScreenDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString selectedPath() const;

private:
    void buildRecentProjects(const QStringList& recentProjects);
    void openMovieDialog();
    void openRecentProject(QListWidgetItem* item);

    QString selectedPath_;
    QListWidget* recentList_ = nullptr;
};

} // namespace libreshockwave::editor
