#include "EditorContext.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <QString>

#include "libreshockwave/DirectorFile.hpp"

namespace libreshockwave::editor {
namespace {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int frameCount(const std::shared_ptr<DirectorFile>& movie) {
    if (!movie || !movie->scoreChunk()) {
        return 1;
    }
    return std::max(1, movie->scoreChunk()->getFrameCount());
}

} // namespace

EditorContext::EditorContext(QObject* parent) : QObject(parent) {}

const std::filesystem::path& EditorContext::currentPath() const {
    return currentPath_;
}

std::shared_ptr<DirectorFile> EditorContext::movie() const {
    return movie_;
}

int EditorContext::currentFrame() const {
    return currentFrame_;
}

bool EditorContext::isPlaying() const {
    return playing_;
}

bool EditorContext::loopPlayback() const {
    return loopPlayback_;
}

const QMap<QString, QString>& EditorContext::externalParams() const {
    return externalParams_;
}

const SelectionState& EditorContext::selection() const {
    return selection_;
}

bool EditorContext::openFile(const std::filesystem::path& path, std::string* errorMessage) {
    try {
        auto loaded = DirectorFile::load(readFileBytes(path));
        loaded->setBasePath(path.parent_path().string());

        currentPath_ = path;
        movie_ = std::move(loaded);
        currentFrame_ = 1;
        playing_ = false;
        selection_ = {};

        emit movieChanged();
        emit frameChanged(currentFrame_);
        emit playbackChanged(playing_);
        emit selectionChanged(selection_);
        emit statusMessageChanged(QStringLiteral("Opened %1").arg(QString::fromStdString(path.filename().string())));
        return true;
    } catch (const std::exception& ex) {
        if (errorMessage) {
            *errorMessage = ex.what();
        }
        emit statusMessageChanged(QStringLiteral("Open failed"));
        return false;
    }
}

void EditorContext::loadExternalParams(QMap<QString, QString> params) {
    externalParams_ = std::move(params);
    emit externalParamsChanged();
}

void EditorContext::closeFile() {
    currentPath_.clear();
    movie_.reset();
    currentFrame_ = 1;
    playing_ = false;
    selection_ = {};
    emit movieChanged();
    emit frameChanged(currentFrame_);
    emit playbackChanged(playing_);
    emit selectionChanged(selection_);
    emit statusMessageChanged(QStringLiteral("Closed movie"));
}

void EditorContext::play() {
    if (!movie_) {
        emit statusMessageChanged(QStringLiteral("No movie loaded"));
        return;
    }
    playing_ = true;
    emit playbackChanged(playing_);
    emit statusMessageChanged(QStringLiteral("Playback started"));
}

void EditorContext::stop() {
    if (!movie_) {
        return;
    }
    playing_ = false;
    emit playbackChanged(playing_);
    emit statusMessageChanged(QStringLiteral("Playback stopped"));
}

void EditorContext::rewind() {
    if (!movie_) {
        return;
    }
    currentFrame_ = 1;
    playing_ = false;
    emit frameChanged(currentFrame_);
    emit playbackChanged(playing_);
}

void EditorContext::goToFrame(int frame) {
    if (!movie_) {
        return;
    }
    currentFrame_ = std::clamp(frame, 1, frameCount(movie_));
    emit frameChanged(currentFrame_);
}

void EditorContext::stepBackward() {
    if (!movie_) {
        return;
    }
    currentFrame_ = std::max(1, currentFrame_ - 1);
    emit frameChanged(currentFrame_);
}

void EditorContext::stepForward() {
    if (!movie_) {
        return;
    }
    currentFrame_ = std::min(frameCount(movie_), currentFrame_ + 1);
    emit frameChanged(currentFrame_);
}

void EditorContext::setLoopPlayback(bool loop) {
    loopPlayback_ = loop;
    emit statusMessageChanged(loopPlayback_ ? QStringLiteral("Loop playback enabled")
                                            : QStringLiteral("Loop playback disabled"));
}

void EditorContext::setExternalParams(QMap<QString, QString> params) {
    externalParams_ = std::move(params);
    emit externalParamsChanged();
    emit statusMessageChanged(QStringLiteral("External parameters updated (%1)").arg(externalParams_.size()));
}

void EditorContext::clearSelection() {
    setSelection({});
}

void EditorContext::selectCastMember(int castLib, int memberNum) {
    setSelection(SelectionState{
        .type = SelectionType::CastMember,
        .castLib = castLib,
        .memberNum = memberNum,
    });
}

void EditorContext::selectFrame(int frame) {
    setSelection(SelectionState{
        .type = SelectionType::Frame,
        .frame = frame,
    });
}

void EditorContext::selectScoreCell(int channel, int frame) {
    setSelection(SelectionState{
        .type = SelectionType::ScoreCell,
        .channel = channel,
        .frame = frame,
    });
}

void EditorContext::selectSprite(int channel, int frame) {
    setSelection(SelectionState{
        .type = SelectionType::Sprite,
        .channel = channel,
        .frame = frame,
    });
}

void EditorContext::setSelection(SelectionState selection) {
    selection_ = selection;
    emit selectionChanged(selection_);
}

} // namespace libreshockwave::editor
