#include "StartScreenDialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "EditorModels.hpp"
#include "EditorPreferences.hpp"

namespace libreshockwave::editor {
namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString recentProjectText(const QString& path) {
    const QFileInfo info(path);
    const QString name = info.fileName().isEmpty() ? path : info.fileName();
    const QString parent = info.dir().absolutePath();
    return QString::fromStdString(startScreenRecentProjectText(name.toStdString(),
                                                               parent.toStdString(),
                                                               info.exists()));
}

} // namespace

StartScreenDialog::StartScreenDialog(QWidget* parent) : QDialog(parent) {
    const auto text = startScreenText();

    setWindowTitle(toQString(text.windowTitle));
    setModal(true);
    setFixedSize(520, 360);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* title = new QLabel(toQString(text.appTitle), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    auto* subtitle = new QLabel(toQString(text.subtitle), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(title);
    root->addWidget(subtitle);

    buildRecentProjects(EditorPreferences::recentProjects());
    root->addWidget(recentList_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* newMovie = new QPushButton(toQString(text.createNewMovieText), this);
    auto* openMovie = new QPushButton(toQString(text.openMovieText), this);
    buttons->addWidget(newMovie);
    buttons->addWidget(openMovie);
    buttons->addStretch(1);
    root->addLayout(buttons);

    connect(newMovie, &QPushButton::clicked, this, [this] {
        const auto text = startScreenText();
        QMessageBox::information(this,
                                 toQString(text.createNewMovieDialogTitle),
                                 toQString(text.createNewMoviePendingText));
    });
    connect(openMovie, &QPushButton::clicked, this, [this] { openMovieDialog(); });
    connect(recentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        openRecentProject(item);
    });
    connect(recentList_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        openRecentProject(item);
    });
}

QString StartScreenDialog::selectedPath() const {
    return selectedPath_;
}

void StartScreenDialog::buildRecentProjects(const QStringList& recentProjects) {
    recentList_ = new QListWidget(this);
    recentList_->setSelectionMode(QAbstractItemView::SingleSelection);

    const auto text = startScreenText();
    if (recentProjects.isEmpty()) {
        auto* item = new QListWidgetItem(toQString(text.emptyRecentProjectsText));
        item->setFlags(Qt::NoItemFlags);
        recentList_->addItem(item);
        return;
    }

    auto* header = new QListWidgetItem(toQString(text.recentProjectsHeader));
    header->setFlags(Qt::NoItemFlags);
    recentList_->addItem(header);

    for (const auto& path : recentProjects) {
        auto* item = new QListWidgetItem(recentProjectText(path));
        item->setData(Qt::UserRole, path);
        if (!QFileInfo::exists(path)) {
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        recentList_->addItem(item);
    }
}

void StartScreenDialog::openMovieDialog() {
    const QString lastDirectory = EditorPreferences::lastOpenDirectory();
    const auto text = startScreenText();
    const QString path = QFileDialog::getOpenFileName(
        this,
        toQString(text.openDirectorFileTitle),
        lastDirectory,
        toQString(text.directorFileFilter));
    if (path.isEmpty()) {
        return;
    }
    EditorPreferences::setLastOpenDirectory(QFileInfo(path).absolutePath());
    selectedPath_ = path;
    accept();
}

void StartScreenDialog::openRecentProject(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) {
        return;
    }
    if (!QFileInfo::exists(path)) {
        const auto text = startScreenText();
        QMessageBox::warning(this,
                             toQString(text.fileNotFoundTitle),
                             toQString(text.fileNotFoundMessagePrefix) + path);
        return;
    }
    selectedPath_ = path;
    accept();
}

} // namespace libreshockwave::editor
