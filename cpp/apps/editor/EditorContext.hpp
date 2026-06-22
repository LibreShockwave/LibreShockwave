#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <QMap>
#include <QObject>
#include <QString>

#include "EditorModels.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::editor {

class EditorContext final : public QObject {
    Q_OBJECT

public:
    explicit EditorContext(QObject* parent = nullptr);

    [[nodiscard]] const std::filesystem::path& currentPath() const;
    [[nodiscard]] std::shared_ptr<DirectorFile> movie() const;
    [[nodiscard]] int currentFrame() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool loopPlayback() const;
    [[nodiscard]] const QMap<QString, QString>& externalParams() const;
    [[nodiscard]] const SelectionState& selection() const;

    bool openFile(const std::filesystem::path& path, std::string* errorMessage = nullptr);
    void loadExternalParams(QMap<QString, QString> params);

public slots:
    void closeFile();
    void play();
    void stop();
    void rewind();
    void goToFrame(int frame);
    void stepBackward();
    void stepForward();
    void setLoopPlayback(bool loop);
    void setExternalParams(QMap<QString, QString> params);
    void clearSelection();
    void selectCastMember(int castLib, int memberNum);
    void selectFrame(int frame);
    void selectScoreCell(int channel, int frame);
    void selectSprite(int channel, int frame);

signals:
    void movieChanged();
    void frameChanged(int frame);
    void playbackChanged(bool playing);
    void externalParamsChanged();
    void selectionChanged(const libreshockwave::editor::SelectionState& selection);
    void statusMessageChanged(const QString& message);

private:
    void setSelection(SelectionState selection);

    std::filesystem::path currentPath_;
    std::shared_ptr<DirectorFile> movie_;
    int currentFrame_ = 1;
    bool playing_ = false;
    bool loopPlayback_ = true;
    QMap<QString, QString> externalParams_;
    SelectionState selection_;
};

} // namespace libreshockwave::editor
