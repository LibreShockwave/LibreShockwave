#include "MainWindow.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <functional>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#endif
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QFrame>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
#include <QMediaPlayer>
#endif
#include <QMessageBox>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPolygon>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QKeySequence>
#include <QSettings>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QSpinBox>
#include <QStyle>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
#include <QTemporaryFile>
#endif
#include <QTabWidget>
#include <QToolBar>
#include <QTimer>
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
#include <QUrl>
#endif
#include <QVBoxLayout>

#include "EditorModels.hpp"
#include "EditorPreferences.hpp"
#include "ExternalParamsDialog.hpp"
#include "StartScreenDialog.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/audio/SoundConverter.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/FrameLabelsChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/vm/parse/LingoExpressionParser.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/audio/SoundManager.hpp"
#include "libreshockwave/player/behavior/BehaviorInstance.hpp"
#include "libreshockwave/player/behavior/BehaviorManager.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/debug/DebugController.hpp"

namespace libreshockwave::editor {

class ScriptPreviewHighlighter final : public QSyntaxHighlighter {
public:
    explicit ScriptPreviewHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
        keywordFormat_.setForeground(QColor(0, 82, 155));
        keywordFormat_.setFontWeight(QFont::Bold);
        commentFormat_.setForeground(QColor(95, 115, 125));
        stringFormat_.setForeground(QColor(155, 65, 0));
        offsetFormat_.setForeground(QColor(120, 80, 170));
        offsetFormat_.setFontWeight(QFont::Bold);
    }

protected:
    void highlightBlock(const QString& text) override {
        const QStringList keywords{
            QStringLiteral("on"),
            QStringLiteral("end"),
            QStringLiteral("if"),
            QStringLiteral("then"),
            QStringLiteral("else"),
            QStringLiteral("repeat"),
            QStringLiteral("while"),
            QStringLiteral("with"),
            QStringLiteral("case"),
            QStringLiteral("return"),
            QStringLiteral("set"),
            QStringLiteral("put"),
            QStringLiteral("global"),
            QStringLiteral("property"),
        };
        for (const auto& keyword : keywords) {
            int index = text.indexOf(keyword, 0, Qt::CaseInsensitive);
            while (index >= 0) {
                const bool leftOk = index == 0 || !text.at(index - 1).isLetterOrNumber();
                const int end = index + keyword.size();
                const bool rightOk = end >= text.size() || !text.at(end).isLetterOrNumber();
                if (leftOk && rightOk) {
                    setFormat(index, keyword.size(), keywordFormat_);
                }
                index = text.indexOf(keyword, end, Qt::CaseInsensitive);
            }
        }

        int quote = text.indexOf(QLatin1Char('"'));
        while (quote >= 0) {
            const int close = text.indexOf(QLatin1Char('"'), quote + 1);
            const int length = close >= 0 ? close - quote + 1 : text.size() - quote;
            setFormat(quote, length, stringFormat_);
            quote = close >= 0 ? text.indexOf(QLatin1Char('"'), close + 1) : -1;
        }

        const int offsetOpen = text.indexOf(QLatin1Char('['));
        const int offsetClose = text.indexOf(QLatin1Char(']'), offsetOpen + 1);
        if (offsetOpen >= 0 && offsetClose > offsetOpen + 1) {
            bool ok = false;
            text.mid(offsetOpen + 1, offsetClose - offsetOpen - 1).toInt(&ok);
            if (ok) {
                setFormat(offsetOpen, offsetClose - offsetOpen + 1, offsetFormat_);
            }
        }

        const int lingoComment = text.indexOf(QStringLiteral("--"));
        const int bytecodeComment = text.indexOf(QStringLiteral("; "));
        int comment = -1;
        if (lingoComment >= 0 && bytecodeComment >= 0) {
            comment = std::min(lingoComment, bytecodeComment);
        } else {
            comment = std::max(lingoComment, bytecodeComment);
        }
        if (comment >= 0) {
            setFormat(comment, text.size() - comment, commentFormat_);
        }
    }

private:
    QTextCharFormat keywordFormat_;
    QTextCharFormat commentFormat_;
    QTextCharFormat stringFormat_;
    QTextCharFormat offsetFormat_;
};

class ScoreMarkerWidget final : public QFrame {
public:
    explicit ScoreMarkerWidget(QWidget* parent = nullptr) : QFrame(parent) {
        setMinimumSize(sizeHint());
    }

    void setMarkers(std::vector<ScoreMarker> markers, int frameCount) {
        markers_ = std::move(markers);
        frameCount_ = std::max(1, frameCount);
        setMinimumSize(sizeHint());
        updateGeometry();
        update();
    }

    void setCurrentFrame(int frame) {
        currentFrame_ = std::clamp(frame, 1, std::max(1, frameCount_));
        update();
    }

    void setMarkerCallback(std::function<void(int)> callback) {
        markerCallback_ = std::move(callback);
    }

    QSize sizeHint() const override {
        return QSize(std::max(1, frameCount_) * scoreCellWidth(), scoreHeaderHeight());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const int cellWidth = scoreCellWidth();
        const int width = std::max(1, frameCount_) * cellWidth;
        painter.fillRect(QRect(0, 0, width, height()), QColor(230, 230, 230));

        painter.setPen(QColor(175, 175, 175));
        for (int frame = 1; frame <= frameCount_; frame += 10) {
            const int x = (frame - 1) * cellWidth;
            painter.drawLine(x, height() - 5, x, height() - 1);
        }

        const int headX = std::clamp(currentFrame_ - 1, 0, std::max(0, frameCount_ - 1)) * cellWidth;
        painter.fillRect(QRect(headX, 0, std::max(2, cellWidth / 5), height()), QColor(255, 214, 90, 130));

        for (const auto& marker : markers_) {
            if (marker.frame < 1 || marker.frame > frameCount_) {
                continue;
            }
            const int x = (marker.frame - 1) * cellWidth;
            const QPoint points[3] = {
                QPoint(x + cellWidth / 2, 2),
                QPoint(x + 2, scoreHeaderHeight() - 3),
                QPoint(x + cellWidth - 2, scoreHeaderHeight() - 3),
            };
            painter.setBrush(QColor(40, 95, 170));
            painter.setPen(QColor(20, 65, 130));
            painter.drawPolygon(points, 3);
            if (!marker.label.empty()) {
                painter.setPen(QColor(35, 35, 35));
                painter.drawText(QRect(x + cellWidth + 2, 0, 140, scoreHeaderHeight()),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 QString::fromStdString(marker.label));
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!markerCallback_) {
            return;
        }
        const int frame = std::clamp((event->pos().x() / scoreCellWidth()) + 1, 1, frameCount_);
        markerCallback_(frame);
    }

private:
    std::vector<ScoreMarker> markers_;
    int frameCount_ = 1;
    int currentFrame_ = 1;
    std::function<void(int)> markerCallback_;
};

class ScoreChannelHeaderWidget final : public QFrame {
public:
    explicit ScoreChannelHeaderWidget(QWidget* parent = nullptr) : QFrame(parent) {
        setMinimumSize(sizeHint());
    }

    void setChannelCount(int channelCount) {
        channelCount_ = std::max(static_cast<int>(scoreSpecialChannelNames().size()), channelCount);
        setMinimumSize(sizeHint());
        updateGeometry();
        update();
    }

    void setSelectedChannel(int channel) {
        selectedChannel_ = channel;
        update();
    }

    void clearSelectedChannel() {
        selectedChannel_ = -1;
        update();
    }

    void setChannelCallback(std::function<void(int)> callback) {
        channelCallback_ = std::move(callback);
    }

    QSize sizeHint() const override {
        return QSize(scoreChannelHeaderWidth(), std::max(1, channelCount_) * scoreCellHeight());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(220, 220, 220));

        const int cellHeight = scoreCellHeight();
        const int specialCount = static_cast<int>(scoreSpecialChannelNames().size());
        for (int channel = 0; channel < channelCount_; ++channel) {
            const QRect rowRect(0, channel * cellHeight, width(), cellHeight);
            const bool special = channel < specialCount;
            painter.fillRect(rowRect, special ? QColor(210, 215, 222) : QColor(232, 232, 232));
            if (channel == selectedChannel_) {
                painter.fillRect(rowRect, QColor(75, 135, 210, 80));
            }
            painter.setPen(QColor(180, 180, 180));
            painter.drawLine(rowRect.bottomLeft(), rowRect.bottomRight());
            painter.setPen(QColor(35, 35, 35));
            QFont labelFont = painter.font();
            labelFont.setBold(special);
            painter.setFont(labelFont);
            painter.drawText(rowRect.adjusted(6, 0, -4, 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromStdString(scoreChannelName(channel)));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!channelCallback_) {
            return;
        }
        const int channel = std::clamp(event->pos().y() / scoreCellHeight(), 0, std::max(0, channelCount_ - 1));
        channelCallback_(channel);
    }

private:
    int channelCount_ = static_cast<int>(scoreSpecialChannelNames().size()) + 48;
    int selectedChannel_ = -1;
    std::function<void(int)> channelCallback_;
};

class ScoreGridWidget final : public QWidget {
public:
    explicit ScoreGridWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(sizeHint());
    }

    void setScoreData(std::vector<ScoreIntervalRow> intervals, int frameCount, int channelCount) {
        intervals_ = std::move(intervals);
        frameCount_ = std::max(1, frameCount);
        channelCount_ = std::max(static_cast<int>(scoreSpecialChannelNames().size()), channelCount);
        setMinimumSize(sizeHint());
        updateGeometry();
        update();
    }

    void setPendingKeyframes(std::vector<PendingScoreKeyframe> keyframes) {
        pendingKeyframes_ = std::move(keyframes);
        update();
    }

    void setPendingRemovedFrames(std::vector<int> frames) {
        pendingRemovedFrames_ = std::move(frames);
        update();
    }

    void setCurrentFrame(int frame) {
        currentFrame_ = std::clamp(frame, 1, std::max(1, frameCount_));
        update();
    }

    void setSelectedCell(int channel, int frame) {
        selectedChannel_ = channel;
        selectedFrame_ = frame;
        update();
    }

    void clearSelectedCell() {
        selectedChannel_ = -1;
        selectedFrame_ = 0;
        update();
    }

    void setSelectionCallback(std::function<void(int, int)> callback) {
        selectionCallback_ = std::move(callback);
    }

    void setHoverCallback(std::function<void(std::optional<int>, std::optional<int>)> callback) {
        hoverCallback_ = std::move(callback);
    }

    void setGridLinesVisible(bool visible) {
        gridLinesVisible_ = visible;
        update();
    }

    QSize sizeHint() const override {
        return QSize(std::max(1, frameCount_) * scoreCellWidth(),
                     std::max(1, channelCount_) * scoreCellHeight());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(250, 250, 250));

        const int cellWidth = scoreCellWidth();
        const int cellHeight = scoreCellHeight();
        const int width = frameCount_ * cellWidth;
        const int height = channelCount_ * cellHeight;

        painter.fillRect(QRect(0, 0, width, height), Qt::white);
        for (const int frame : pendingRemovedFrames_) {
            if (frame < 1 || frame > frameCount_) {
                continue;
            }
            const int x = (frame - 1) * cellWidth;
            const QRect frameRect(x, 0, cellWidth, height);
            painter.fillRect(frameRect, QColor(214, 54, 43, 54));
            painter.setPen(QPen(QColor(170, 45, 35), 2));
            painter.drawLine(frameRect.topLeft(), frameRect.bottomRight());
            painter.drawLine(frameRect.topRight(), frameRect.bottomLeft());
        }
        if (gridLinesVisible_) {
            painter.setPen(QColor(225, 225, 225));
            for (int channel = 0; channel <= channelCount_; ++channel) {
                const int y = channel * cellHeight;
                painter.drawLine(0, y, width, y);
            }
            for (int frame = 0; frame <= frameCount_; ++frame) {
                const int x = frame * cellWidth;
                painter.setPen(frame % 10 == 0 ? QColor(185, 185, 185) : QColor(232, 232, 232));
                painter.drawLine(x, 0, x, height);
            }
        }

        for (const auto& interval : intervals_) {
            if (interval.channel < 0 || interval.channel >= channelCount_) {
                continue;
            }
            const int x = std::max(0, interval.startFrame - 1) * cellWidth;
            const int y = interval.channel * cellHeight;
            const int span = std::max(1, interval.endFrame - interval.startFrame + 1);
            QRect cellRect(x + 1, y + 2, span * cellWidth - 2, cellHeight - 4);
            painter.fillRect(cellRect, colorFromRgbValue(scoreIntervalBackgroundRgb(interval)));
            painter.setPen(QColor(105, 105, 105));
            painter.drawRect(cellRect.adjusted(0, 0, -1, -1));

            if (!interval.memberName.empty() && cellRect.width() > 34) {
                painter.setPen(QColor(35, 35, 35));
                painter.drawText(cellRect.adjusted(4, 0, -4, 0),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 QString::fromStdString(interval.memberName));
            }
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const auto& keyframe : pendingKeyframes_) {
            if (keyframe.channel < 0 || keyframe.channel >= channelCount_ ||
                keyframe.frame < 1 || keyframe.frame > frameCount_) {
                continue;
            }
            const int x = (keyframe.frame - 1) * cellWidth;
            const int y = keyframe.channel * cellHeight;
            const QPoint center(x + cellWidth / 2, y + cellHeight / 2);
            QPolygon diamond;
            diamond << QPoint(center.x(), y + 3)
                    << QPoint(x + cellWidth - 4, center.y())
                    << QPoint(center.x(), y + cellHeight - 4)
                    << QPoint(x + 3, center.y());
            painter.setBrush(QColor(255, 176, 35, 210));
            painter.setPen(QPen(QColor(145, 87, 0), 1));
            painter.drawPolygon(diamond);
        }
        painter.setRenderHint(QPainter::Antialiasing, false);

        const int headX = std::clamp(currentFrame_ - 1, 0, std::max(0, frameCount_ - 1)) * cellWidth;
        painter.fillRect(QRect(headX, 0, std::max(2, cellWidth / 5), height), QColor(255, 214, 90, 130));
        painter.setPen(QPen(QColor(190, 125, 0), 2));
        painter.drawLine(headX, 0, headX, height);

        if (selectedChannel_ >= 0 && selectedChannel_ < channelCount_ && selectedFrame_ >= 1 && selectedFrame_ <= frameCount_) {
            const QRect selectedRect((selectedFrame_ - 1) * cellWidth,
                                     selectedChannel_ * cellHeight,
                                     cellWidth,
                                     cellHeight);
            painter.setPen(QPen(QColor(20, 90, 180), 2));
            painter.drawRect(selectedRect.adjusted(1, 1, -2, -2));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!selectionCallback_) {
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const auto [channel, frame] = cellAt(event->pos());
        selectionCallback_(channel, frame);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!selectionCallback_) {
            QWidget::keyPressEvent(event);
            return;
        }

        int frame = selectedFrame_ > 0 ? selectedFrame_ : currentFrame_;
        int channel = selectedChannel_ >= 0 ? selectedChannel_ : 0;
        switch (event->key()) {
        case Qt::Key_Left:
            frame = std::max(1, frame - 1);
            break;
        case Qt::Key_Right:
            frame = std::min(frameCount_, frame + 1);
            break;
        case Qt::Key_Up:
            channel = std::max(0, channel - 1);
            break;
        case Qt::Key_Down:
            channel = std::min(channelCount_ - 1, channel + 1);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
        }

        selectionCallback_(channel, frame);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!hoverCallback_) {
            return;
        }
        const auto [channel, frame] = cellAt(event->pos());
        hoverCallback_(frame, channel);
    }

    void leaveEvent(QEvent*) override {
        if (hoverCallback_) {
            hoverCallback_(std::nullopt, std::nullopt);
        }
    }

private:
    std::pair<int, int> cellAt(const QPoint& position) const {
        const int frame = std::clamp((position.x() / scoreCellWidth()) + 1, 1, frameCount_);
        const int channel = std::clamp(position.y() / scoreCellHeight(), 0, std::max(0, channelCount_ - 1));
        return {channel, frame};
    }

    static QColor colorFromRgbValue(std::uint32_t rgb) {
        return QColor(static_cast<int>((rgb >> 16) & 0xFF),
                      static_cast<int>((rgb >> 8) & 0xFF),
                      static_cast<int>(rgb & 0xFF));
    }

    std::vector<ScoreIntervalRow> intervals_;
    std::vector<PendingScoreKeyframe> pendingKeyframes_;
    std::vector<int> pendingRemovedFrames_;
    int frameCount_ = 1;
    int channelCount_ = static_cast<int>(scoreSpecialChannelNames().size()) + 48;
    int currentFrame_ = 1;
    int selectedChannel_ = -1;
    int selectedFrame_ = 0;
    bool gridLinesVisible_ = true;
    std::function<void(int, int)> selectionCallback_;
    std::function<void(std::optional<int>, std::optional<int>)> hoverCallback_;
};

namespace {

constexpr int kBytecodeOffsetRole = Qt::UserRole;
constexpr int kBytecodeCallTargetRole = Qt::UserRole + 1;
constexpr int kBehaviorScriptIdRole = Qt::UserRole + 2;
constexpr int kBehaviorPendingActionRole = Qt::UserRole + 3;
constexpr int kBehaviorPendingFrameRole = Qt::UserRole + 4;
constexpr int kBehaviorPendingChannelRole = Qt::UserRole + 5;
constexpr int kCastDetailsRole = Qt::UserRole;
constexpr int kCastTargetPanelRole = Qt::UserRole + 1;
constexpr int kCastMemberNumberRole = Qt::UserRole + 2;
constexpr int kCastLibraryNumberRole = Qt::UserRole + 3;
constexpr int kCastDisplayNameRole = Qt::UserRole + 4;

struct CastSelectorEntry {
    int castLibNumber = 0;
    QString label;
};

void addSeparator(QMenu* menu) {
    menu->addSeparator();
}

QKeySequence debugCommandShortcut(std::string_view shortcut) {
    if (shortcut.empty()) {
        return {};
    }
    if (shortcut == "F11") {
        return QKeySequence(Qt::Key_F11);
    }
    if (shortcut == "F10") {
        return QKeySequence(Qt::Key_F10);
    }
    if (shortcut == "Shift+F11") {
        return QKeySequence(Qt::SHIFT | Qt::Key_F11);
    }
    if (shortcut == "F5") {
        return QKeySequence(Qt::Key_F5);
    }
    if (shortcut == "F9") {
        return QKeySequence(Qt::Key_F9);
    }
    if (shortcut == "Ctrl+Shift+S") {
        return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S);
    }
    if (shortcut == "Ctrl+Shift+T") {
        return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T);
    }
    return QKeySequence(QString::fromUtf8(shortcut.data(), static_cast<int>(shortcut.size())));
}

QKeySequence playbackCommandShortcut(std::string_view shortcut) {
    if (shortcut.empty()) {
        return {};
    }
    if (shortcut == "Ctrl+Alt+P") {
        return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P);
    }
    if (shortcut == "Ctrl+.") {
        return QKeySequence(Qt::CTRL | Qt::Key_Period);
    }
    if (shortcut == "Ctrl+Alt+R") {
        return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R);
    }
    if (shortcut == "Ctrl+Alt+Right") {
        return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right);
    }
    if (shortcut == "Ctrl+Alt+Left") {
        return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left);
    }
    return QKeySequence(QString::fromUtf8(shortcut.data(), static_cast<int>(shortcut.size())));
}

QKeySequence shortcutFromSpec(std::string_view shortcut) {
    return QKeySequence(QString::fromUtf8(shortcut.data(), static_cast<int>(shortcut.size())));
}

void connectPlaybackCommand(QAction* action, EditorContext& context, std::string_view commandId) {
    if (commandId == "play") {
        QObject::connect(action, &QAction::triggered, &context, &EditorContext::play);
    } else if (commandId == "stop") {
        QObject::connect(action, &QAction::triggered, &context, &EditorContext::stop);
    } else if (commandId == "rewind") {
        QObject::connect(action, &QAction::triggered, &context, &EditorContext::rewind);
    } else if (commandId == "step-forward") {
        QObject::connect(action, &QAction::triggered, &context, &EditorContext::stepForward);
    } else if (commandId == "step-backward") {
        QObject::connect(action, &QAction::triggered, &context, &EditorContext::stepBackward);
    }
}

QIcon playbackCommandIcon(const QStyle& style, std::string_view commandId) {
    if (commandId == "play") {
        return style.standardIcon(QStyle::SP_MediaPlay);
    }
    if (commandId == "stop") {
        return style.standardIcon(QStyle::SP_MediaStop);
    }
    if (commandId == "rewind") {
        return style.standardIcon(QStyle::SP_MediaSkipBackward);
    }
    if (commandId == "step-forward") {
        return style.standardIcon(QStyle::SP_MediaSeekForward);
    }
    if (commandId == "step-backward") {
        return style.standardIcon(QStyle::SP_MediaSeekBackward);
    }
    return {};
}

QString toQString(const std::string& value) {
    return QString::fromStdString(value);
}

QString toQString(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::vector<CastSelectorEntry> runtimeCastSelectorEntries(player::Player* player) {
    std::vector<CastSelectorEntry> entries;
    if (!player) {
        entries.push_back(CastSelectorEntry{.castLibNumber = 0, .label = toQString(castWindowDefaultCastName())});
        return entries;
    }

    for (const auto& [number, castLib] : player->castLibManager().castLibs()) {
        if (!castLib) {
            continue;
        }
        const bool internalCast = number == 1 && !castLib->isExternal();
        const QString label = toQString(castWindowCastLibraryLabel(number,
                                                                   castLib->name(),
                                                                   internalCast,
                                                                   castLib->isExternal(),
                                                                   castLib->isLoaded()));
        entries.push_back(CastSelectorEntry{
            .castLibNumber = internalCast ? 0 : number,
            .label = label,
        });
    }
    if (entries.empty()) {
        entries.push_back(CastSelectorEntry{.castLibNumber = 0, .label = toQString(castWindowDefaultCastName())});
    }
    return entries;
}

CastMemberRow castMemberRowFromChunk(int memberNumber, const chunks::CastMemberChunk& member) {
    return CastMemberRow{
        .chunkId = memberNumber,
        .type = memberTypeDisplayName(member.memberType()),
        .name = member.name(),
        .memberType = member.memberType(),
        .scriptId = member.scriptId(),
        .regPointX = member.regPointX(),
        .regPointY = member.regPointY(),
        .specificDataSize = static_cast<int>(member.specificData().size()),
    };
}

CastMemberRow castMemberRowFromRuntimeMember(const cast::CastMember& member) {
    return CastMemberRow{
        .chunkId = member.memberNum(),
        .type = memberTypeDisplayName(member.memberType()),
        .name = member.name(),
        .memberType = member.memberType(),
        .scriptId = member.scriptId(),
        .regPointX = member.regX(),
        .regPointY = member.regY(),
    };
}

std::vector<CastMemberRow> buildRuntimeCastMemberRows(player::cast::CastLib& castLib, std::string_view filter) {
    std::vector<CastMemberRow> rows;
    if (!castLib.isLoaded()) {
        return rows;
    }
    for (const auto& [memberNumber, member] : castLib.memberChunks()) {
        if (!member || member->memberType() == cast::MemberType::Null) {
            continue;
        }
        auto row = castMemberRowFromChunk(memberNumber, *member);
        if (castMemberMatchesFilter(row, filter)) {
            rows.push_back(std::move(row));
        }
    }
    for (const auto& [memberNumber, member] : castLib.runtimeMembers()) {
        if (!member || !member->isRuntimeDynamic() || member->isReusableDynamicSlot()) {
            continue;
        }
        if (castLib.memberChunks().contains(memberNumber)) {
            continue;
        }
        auto row = castMemberRowFromRuntimeMember(*member);
        if (castMemberMatchesFilter(row, filter)) {
            rows.push_back(std::move(row));
        }
    }
    std::sort(rows.begin(), rows.end(), [](const CastMemberRow& left, const CastMemberRow& right) {
        return left.chunkId < right.chunkId;
    });
    return rows;
}

QTableWidgetItem* item(const QString& text) {
    auto* tableItem = new QTableWidgetItem(text);
    tableItem->setFlags(tableItem->flags() & ~Qt::ItemIsEditable);
    return tableItem;
}

QTableWidgetItem* item(int value) {
    return item(QString::number(value));
}

QColor colorFromRgb(std::uint32_t rgb) {
    return QColor(static_cast<int>((rgb >> 16U) & 0xFFU),
                  static_cast<int>((rgb >> 8U) & 0xFFU),
                  static_cast<int>(rgb & 0xFFU));
}

QPixmap paletteSwatchPixmap(const PaletteSwatch& swatch) {
    QPixmap pixmap(std::max(1, swatch.width), std::max(1, swatch.height));
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    for (const auto& cell : swatch.cells) {
        painter.setBrush(QColor(static_cast<int>((cell.rgb >> 16U) & 0xFFU),
                                static_cast<int>((cell.rgb >> 8U) & 0xFFU),
                                static_cast<int>(cell.rgb & 0xFFU)));
        painter.drawRect(cell.x, cell.y, cell.size, cell.size);
    }
    return pixmap;
}

QColor shapeIndexedColor(int index, bool background) {
    if (background) {
        const int value = std::clamp(235 - (index % 48), 180, 245);
        return QColor(value, value, value);
    }
    const int hue = (index * 37) % 360;
    return QColor::fromHsv(hue, 170, 130);
}

QPixmap vectorShapePreviewPixmap(const cast::ShapeInfo& shape) {
    const int shapeWidth = std::max(24, shape.width);
    const int shapeHeight = std::max(24, shape.height);
    const int canvasWidth = std::clamp(shapeWidth + 80, 180, 640);
    const int canvasHeight = std::clamp(shapeHeight + 80, 140, 480);
    QPixmap pixmap(canvasWidth, canvasHeight);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(pixmap.rect(), QColor(248, 248, 248));
    painter.setPen(QPen(QColor(220, 220, 220), 1, Qt::DashLine));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));

    const QRect rect((canvasWidth - shapeWidth) / 2,
                     (canvasHeight - shapeHeight) / 2,
                     shapeWidth,
                     shapeHeight);
    const QPen outline(shapeIndexedColor(shape.color, false), std::max(1, shape.lineThickness));
    painter.setPen(shape.isOutlineInvisible() ? Qt::NoPen : outline);
    painter.setBrush(shape.isFilled() ? QBrush(shapeIndexedColor(shape.backColor, true)) : Qt::NoBrush);

    switch (shape.shapeType) {
        case cast::ShapeType::Line:
            if (shape.lineDirection == 1) {
                painter.drawLine(rect.bottomLeft(), rect.topRight());
            } else {
                painter.drawLine(rect.topLeft(), rect.bottomRight());
            }
            break;
        case cast::ShapeType::Oval:
            painter.drawEllipse(rect);
            break;
        case cast::ShapeType::OvalRect:
            painter.drawRoundedRect(rect, 18, 18);
            break;
        case cast::ShapeType::Rect:
        case cast::ShapeType::Unknown:
            painter.drawRect(rect);
            break;
    }

    painter.setPen(QColor(90, 90, 90));
    painter.drawText(QRect(8, 8, canvasWidth - 16, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     toQString(vectorShapeTypeName(shape.shapeType)));
    return pixmap;
}

cast::ShapeInfo vectorShapeInfoFromMember(const cast::CastMember& member) {
    return cast::ShapeInfo{
        member.shapeType(),
        member.regX(),
        member.regY(),
        member.width(),
        member.height(),
        0,
        1,
        member.shapeFilled() ? 1 : 0,
        member.shapeLineSize(),
        0,
    };
}

QString normalizedTextQString(std::string_view text) {
    QString result = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.replace(QChar('\r'), QChar('\n'));
    return result;
}

void setTextEditorContent(QTextEdit* editor, const chunks::TextChunk* textChunk) {
    if (!editor) {
        return;
    }
    if (!textChunk) {
        editor->setPlainText(toQString(textEditorNoDataText()));
        return;
    }
    if (textChunk->runs().empty()) {
        editor->setPlainText(toQString(textEditorContent(textChunk)));
        return;
    }

    editor->clear();
    QTextCursor cursor(editor->document());
    const auto& raw = textChunk->text();
    int offset = 0;
    auto insertRange = [&](int start, int end, const QTextCharFormat& format) {
        start = std::clamp(start, 0, static_cast<int>(raw.size()));
        end = std::clamp(end, start, static_cast<int>(raw.size()));
        if (end <= start) {
            return;
        }
        cursor.insertText(normalizedTextQString(std::string_view(raw).substr(static_cast<std::size_t>(start),
                                                                             static_cast<std::size_t>(end - start))),
                          format);
    };

    QTextCharFormat defaultFormat;
    defaultFormat.setFontFamilies({QStringLiteral("SansSerif")});
    defaultFormat.setFontPointSize(14);
    for (const auto& run : textChunk->runs()) {
        if (run.startOffset > offset) {
            insertRange(offset, run.startOffset, defaultFormat);
        }
        QTextCharFormat format = defaultFormat;
        if (run.fontSize > 0) {
            format.setFontPointSize(run.fontSize);
        }
        format.setForeground(QColor(run.colorR, run.colorG, run.colorB));
        format.setFontWeight((run.fontStyle & 0x01) != 0 ? QFont::Bold : QFont::Normal);
        format.setFontItalic((run.fontStyle & 0x02) != 0);
        format.setFontUnderline((run.fontStyle & 0x04) != 0);
        insertRange(run.startOffset, run.endOffset, format);
        offset = std::max(offset, run.endOffset);
    }
    if (offset < static_cast<int>(raw.size())) {
        insertRange(offset, static_cast<int>(raw.size()), defaultFormat);
    }
    editor->moveCursor(QTextCursor::Start);
}

QImage bitmapToImage(const bitmap::Bitmap& bitmap) {
    if (bitmap.width() <= 0 || bitmap.height() <= 0 || bitmap.pixels().empty()) {
        return {};
    }

    QImage image(bitmap.width(), bitmap.height(), QImage::Format_ARGB32);
    for (int y = 0; y < bitmap.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < bitmap.width(); ++x) {
            const auto argb = bitmap.pixels()[static_cast<std::size_t>(y * bitmap.width() + x)];
            line[x] = static_cast<QRgb>(argb);
        }
    }
    return image;
}

std::shared_ptr<bitmap::Bitmap> imageToRuntimeBitmap(const QImage& image) {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return nullptr;
    }
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    std::vector<std::uint32_t> pixels;
    pixels.reserve(static_cast<std::size_t>(argb.width() * argb.height()));
    for (int y = 0; y < argb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            pixels.push_back(static_cast<std::uint32_t>(line[x]));
        }
    }
    return std::make_shared<bitmap::Bitmap>(argb.width(), argb.height(), 32, std::move(pixels));
}

void drawCheckerboard(QPainter& painter, int size) {
    painter.fillRect(0, 0, size, size, Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(204, 204, 204));
    constexpr int cell = 8;
    for (int y = 0; y < size; y += cell) {
        for (int x = 0; x < size; x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0) {
                painter.drawRect(x, y, cell, cell);
            }
        }
    }
}

void drawCheckerboard(QPainter& painter, const QSize& size) {
    painter.fillRect(QRect(QPoint(0, 0), size), Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(204, 204, 204));
    constexpr int cell = 8;
    for (int y = 0; y < size.height(); y += cell) {
        for (int x = 0; x < size.width(); x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0) {
                painter.drawRect(x, y, cell, cell);
            }
        }
    }
}

QString bytecodePreviewWithAnnotationPreference(const QString& text) {
    if (EditorPreferences::preferenceBool(QStringLiteral("script"), QStringLiteral("showBytecodeAnnotations"), true)) {
        return text;
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        const int bracket = line.indexOf(QLatin1Char('['));
        const int close = line.indexOf(QLatin1Char(']'), bracket + 1);
        if (bracket < 0 || close <= bracket + 1) {
            continue;
        }

        const int semicolon = line.indexOf(QStringLiteral(" ; "), close + 1);
        if (semicolon >= 0) {
            line = line.left(semicolon);
            while (line.endsWith(QLatin1Char(' '))) {
                line.chop(1);
            }
            continue;
        }
        const int doubleDash = line.indexOf(QStringLiteral("  -- "), close + 1);
        if (doubleDash >= 0) {
            line = line.left(doubleDash);
            while (line.endsWith(QLatin1Char(' '))) {
                line.chop(1);
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QPixmap placeholderThumbnail(const QString& abbreviation) {
    const int size = castThumbnailSize();
    QPixmap pixmap(size, size);
    pixmap.fill(QColor(240, 240, 240));
    QPainter painter(&pixmap);
    painter.setPen(Qt::gray);
    painter.drawRect(0, 0, size - 1, size - 1);
    QFont font(QStringLiteral("SansSerif"));
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);
    painter.setPen(Qt::darkGray);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, abbreviation.isEmpty() ? QStringLiteral("?") : abbreviation);
    return pixmap;
}

QPixmap bitmapThumbnailPixmap(const bitmap::Bitmap& bitmap) {
    const int size = castThumbnailSize();
    QPixmap pixmap(size, size);
    QPainter painter(&pixmap);
    drawCheckerboard(painter, size);
    const auto image = bitmapToImage(bitmap);
    if (image.isNull()) {
        return pixmap;
    }
    const auto scaled = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (size - scaled.width()) / 2;
    const int y = (size - scaled.height()) / 2;
    painter.drawImage(x, y, scaled);
    return pixmap;
}

int playbackIntervalMs(const std::shared_ptr<DirectorFile>& movie) {
    if (!movie) {
        return 1000;
    }
    const auto summary = buildMovieSummary(*movie);
    return std::clamp(1000 / std::max(1, summary.tempo), 16, 1000);
}

struct ExportPayload {
    std::vector<std::uint8_t> bytes;
    QString extension;
};

std::optional<ExportPayload> soundExportPayload(const chunks::SoundChunk& sound) {
    if (sound.isMp3()) {
        auto mp3 = audio::SoundConverter::extractMp3(sound);
        if (mp3 && !mp3->empty()) {
            return ExportPayload{.bytes = std::move(*mp3), .extension = QStringLiteral("mp3")};
        }
        return std::nullopt;
    }

    auto wav = audio::SoundConverter::toWav(sound);
    if (wav.size() > 44U) {
        return ExportPayload{.bytes = std::move(wav), .extension = QStringLiteral("wav")};
    }
    return std::nullopt;
}

QString castMemberExportFileNameForMovie(DirectorFile& movie,
                                         const CastMemberRow& row,
                                         const std::shared_ptr<chunks::CastMemberChunk>& resolvedMember = nullptr) {
    if (row.memberType == cast::MemberType::Sound) {
        const auto member = resolvedMember ? resolvedMember : movie.getCastMemberByNumber(0, row.chunkId);
        if (const auto sound = member ? player::audio::SoundManager::findSoundForMember(movie, member) : nullptr) {
            if (const auto payload = soundExportPayload(*sound)) {
                return toQString(castMemberExportFileNameWithExtension(row, payload->extension.toStdString()));
            }
        }
    }
    return toQString(castMemberExportFileName(row));
}

QStringList toQStringList(std::span<const std::string_view> values) {
    QStringList result;
    for (const auto value : values) {
        result.push_back(toQString(value));
    }
    return result;
}

QTableWidget* makeReadOnlyTable(std::span<const std::string_view> columns, QWidget* parent) {
    auto* table = new QTableWidget(0, static_cast<int>(columns.size()), parent);
    table->setHorizontalHeaderLabels(toQStringList(columns));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setShowGrid(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(22);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(11);
    table->setFont(monoFont);
    table->horizontalHeader()->setStretchLastSection(true);
    if (columns.size() == 3 && columns[0] == "#" && columns[1] == "Type") {
        table->setColumnWidth(0, 40);
        table->setColumnWidth(1, 80);
        table->setColumnWidth(2, 220);
    } else if (columns.size() == 3 && columns[0] == "Name") {
        table->setColumnWidth(0, 120);
        table->setColumnWidth(1, 80);
        table->setColumnWidth(2, 220);
    } else if (columns.size() == 3 && columns[0] == "Expression") {
        table->setColumnWidth(0, 160);
        table->setColumnWidth(1, 80);
        table->setColumnWidth(2, 220);
    } else if (columns.size() == 5 && columns[0] == "Name") {
        table->setColumnWidth(0, 120);
        table->setColumnWidth(1, 90);
        table->setColumnWidth(2, 120);
        table->setColumnWidth(3, 180);
        table->setColumnWidth(4, 80);
    } else if (columns.size() == 2 && columns[0] == "Property") {
        table->setColumnWidth(0, 140);
        table->setColumnWidth(1, 220);
    }
    return table;
}

void setDebugTableRows(QTableWidget* table, const std::vector<DebugTableRow>& rows) {
    if (!table) {
        return;
    }
    table->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const auto& cells = rows[row].cells;
        for (int column = 0; column < table->columnCount(); ++column) {
            const auto text = static_cast<std::size_t>(column) < cells.size()
                                  ? toQString(cells[static_cast<std::size_t>(column)])
                                  : QString{};
            auto* tableItem = item(text);
            if (column == 0 && !rows[row].id.empty()) {
                tableItem->setData(Qt::UserRole, toQString(rows[row].id));
            }
            if (!rows[row].detailText.empty()) {
                tableItem->setData(Qt::UserRole + 1, toQString(rows[row].detailTitle));
                tableItem->setData(Qt::UserRole + 2, toQString(rows[row].detailType));
                tableItem->setData(Qt::UserRole + 3, toQString(rows[row].detailText));
                tableItem->setToolTip(toQString(rows[row].detailText));
            }
            table->setItem(static_cast<int>(row), column, tableItem);
        }
    }
    table->resizeRowsToContents();
}

QWidget* makePropertyGrid(std::span<const std::string_view> labels, QWidget* parent, QList<QLineEdit*>* fields = nullptr) {
    auto* panel = new QWidget(parent);
    auto* layout = new QGridLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(4);
    int row = 0;
    for (const auto label : labels) {
        layout->addWidget(new QLabel(toQString(label), panel), row, 0);
        auto* value = new QLineEdit(toQString(propertyInspectorUnsetValueText()), panel);
        value->setReadOnly(true);
        layout->addWidget(value, row, 1);
        if (fields != nullptr) {
            fields->push_back(value);
        }
        ++row;
    }
    layout->setColumnStretch(1, 1);
    layout->setRowStretch(row, 1);
    return panel;
}

void setPropertyGridValues(const QList<QLineEdit*>& fields, std::span<const std::string> values) {
    for (int index = 0; index < fields.size(); ++index) {
        const auto text = static_cast<std::size_t>(index) < values.size() ? values[static_cast<std::size_t>(index)]
                                                                         : propertyInspectorUnsetValueText();
        fields[index]->setText(toQString(text));
    }
}

void clearPropertyGridValues(const QList<QLineEdit*>& fields) {
    for (auto* field : fields) {
        field->setText(toQString(propertyInspectorUnsetValueText()));
    }
}

void setPropertyGridFieldValue(const QList<QLineEdit*>& fields, int index, const QString& value) {
    if (index < 0 || index >= fields.size()) {
        return;
    }
    fields[index]->setText(value);
}

QString boolText(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString datumBoolText(const lingo::Datum& value) {
    return value.isVoid() ? toQString(propertyInspectorUnsetValueText()) : boolText(value.boolValue());
}

void setListValues(QListWidget* list, std::span<const std::string> values) {
    if (!list) {
        return;
    }
    list->clear();
    for (const auto& value : values) {
        list->addItem(toQString(value));
    }
}

std::vector<ExternalParamRow> externalParamRows(const QMap<QString, QString>& params) {
    std::vector<ExternalParamRow> rows;
    rows.reserve(static_cast<std::size_t>(params.size()));
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        rows.push_back(ExternalParamRow{.key = it.key().toStdString(), .value = it.value().toStdString()});
    }
    return rows;
}

QMap<QString, QString> habboPresetParams() {
    QMap<QString, QString> params;
    for (const auto& param : habboExternalParamPreset()) {
        params.insert(QString::fromStdString(param.key), QString::fromStdString(param.value));
    }
    return params;
}

std::string localHttpRootForMoviePath(const std::filesystem::path& path) {
    std::string normalized = path.string();
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    std::string lower = normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    const std::string marker = "/htdocs/";
    const auto markerPos = lower.find(marker);
    if (markerPos == std::string::npos) {
        return {};
    }
    return normalized.substr(0, markerPos + marker.size() - 1);
}

int browserKeyCodeFromQtKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return key;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return key;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return 112 + (key - Qt::Key_F1);
    }

    switch (key) {
        case Qt::Key_Backspace: return 8;
        case Qt::Key_Tab: return 9;
        case Qt::Key_Return:
        case Qt::Key_Enter: return 13;
        case Qt::Key_Escape: return 27;
        case Qt::Key_Space: return 32;
        case Qt::Key_PageUp: return 33;
        case Qt::Key_PageDown: return 34;
        case Qt::Key_End: return 35;
        case Qt::Key_Home: return 36;
        case Qt::Key_Left: return 37;
        case Qt::Key_Up: return 38;
        case Qt::Key_Right: return 39;
        case Qt::Key_Down: return 40;
        case Qt::Key_Delete: return 46;
        default: return key;
    }
}

void applyCastWindowViewMode(QTableWidget* table, const CastWindowViewModeSpec& spec) {
    if (!table) {
        return;
    }
    table->setColumnHidden(1, !spec.showDetailColumns);
    table->setColumnHidden(2, !spec.showDetailColumns);
    table->setColumnHidden(4, !spec.showDetailColumns);
    table->setColumnHidden(5, !spec.showDetailColumns);
    table->setIconSize(QSize(castThumbnailSize(), castThumbnailSize()));
    table->setColumnWidth(0, spec.previewColumnWidth);
    for (int row = 0; row < table->rowCount(); ++row) {
        table->setRowHeight(row, spec.rowHeight);
    }
}

std::optional<ScoreIntervalRow> findScoreIntervalForSelection(DirectorFile& movie, const SelectionState& selection) {
    if (selection.type != SelectionType::ScoreCell && selection.type != SelectionType::Sprite) {
        return std::nullopt;
    }

    for (const auto& interval : buildScoreIntervalRows(movie)) {
        if (interval.channel == selection.channel &&
            selection.frame >= interval.startFrame &&
            selection.frame <= interval.endFrame) {
            return interval;
        }
    }
    return std::nullopt;
}

Qt::DockWidgetArea qtDockArea(DockArea area) {
    switch (area) {
        case DockArea::Left:
            return Qt::LeftDockWidgetArea;
        case DockArea::Right:
            return Qt::RightDockWidgetArea;
        case DockArea::Top:
            return Qt::TopDockWidgetArea;
        case DockArea::Bottom:
            return Qt::BottomDockWidgetArea;
        case DockArea::Center:
            return Qt::NoDockWidgetArea;
    }
    return Qt::NoDockWidgetArea;
}

void setRowData(QTableWidget* table, int row, const CastMemberRow& member, const QString& details, int castLibNumber) {
    const auto targetPanel = toQString(editorPanelForMemberType(member.memberType));
    for (int column = 0; column < table->columnCount(); ++column) {
        if (auto* tableItem = table->item(row, column)) {
            tableItem->setData(kCastDetailsRole, details);
            tableItem->setData(kCastTargetPanelRole, targetPanel);
            tableItem->setData(kCastMemberNumberRole, member.chunkId);
            tableItem->setData(kCastLibraryNumberRole, castLibNumber);
        }
    }
}

void setRowData(QTableWidget* table, int row, const ScoreIntervalRow& interval) {
    const auto background = colorFromRgb(scoreIntervalBackgroundRgb(interval));
    const auto tooltip = toQString(scoreIntervalTooltip(interval));
    for (int column = 0; column < table->columnCount(); ++column) {
        if (auto* tableItem = table->item(row, column)) {
            tableItem->setData(Qt::UserRole, interval.startFrame);
            tableItem->setData(Qt::UserRole + 1, interval.channel);
            tableItem->setData(Qt::UserRole + 2, interval.endFrame);
            tableItem->setData(Qt::UserRole + 3, background);
            tableItem->setBackground(background);
            tableItem->setToolTip(tooltip);
        }
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("LibreShockwaveEditorMainWindow"));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    debugController_ = std::make_shared<::libreshockwave::player::debug::DebugController>();

    const auto shellText = mainWindowShellText();
    auto* center = new QLabel(toQString(shellText.centerPlaceholderText), this);
    center->setAlignment(Qt::AlignCenter);
    center->setMinimumSize(420, 280);
    setCentralWidget(center);

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_, 1);

    createMenus();
    createToolbar();
    createPanels();
    stageZoomPercent_ = EditorPreferences::stageZoomPercent();
    for (auto* action : stageZoomActions_) {
        if (action && action->data().toInt() == stageZoomPercent_) {
            action->setChecked(true);
        }
    }
    setScoreKeyframesVisible(EditorPreferences::viewOption(QStringLiteral("scoreKeyframes"), true));
    setScoreGridLinesVisible(EditorPreferences::viewOption(QStringLiteral("scoreGridLines"), true));
    setSpriteOverlayInfoVisible(EditorPreferences::viewOption(
        QStringLiteral("spriteOverlayInfo"),
        EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("showOverlays"), true)));
    setSpriteOverlayPathsVisible(EditorPreferences::viewOption(
        QStringLiteral("spriteOverlayPaths"),
        EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("showPaths"), false)));
    setSpriteToolbarVisible(EditorPreferences::viewOption(QStringLiteral("spriteToolbar"), true));
    setStageGridSnapEnabled(EditorPreferences::viewOption(
        QStringLiteral("stageGridSnap"),
        EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("snapToGrid"), false)));
    setStageGuidesVisible(EditorPreferences::viewOption(QStringLiteral("stageGuides"), false));
    setStageGuidesSnapEnabled(EditorPreferences::viewOption(QStringLiteral("stageGuidesSnap"), false));
    if (statusLabel_) {
        statusLabel_->clear();
    }
    playbackTimer_ = new QTimer(this);
    playbackTimer_->setTimerType(Qt::PreciseTimer);
    connect(playbackTimer_, &QTimer::timeout, this, &MainWindow::advancePlaybackFrame);
    connectContext();

    resize(1280, 900);
    if (!restoreSavedLayout()) {
        applyDefaultLayout();
    }
    updateWindowTitle();
    updateMovieViews();
    if (debugController_) {
        debugController_->addListener(this);
    }
}

MainWindow::~MainWindow() {
    if (debugController_) {
        debugController_->removeListener(this);
    }
}

void MainWindow::onPaused(const ::libreshockwave::player::debug::DebugSnapshot& snapshot) {
    QMetaObject::invokeMethod(this, [this, snapshot] { updateDetailedStackFromSnapshot(snapshot); }, Qt::QueuedConnection);
}

void MainWindow::onResumed() {
    QMetaObject::invokeMethod(this, [this] { markDetailedStackRunning(); }, Qt::QueuedConnection);
}

void MainWindow::onBreakpointsChanged() {
    QMetaObject::invokeMethod(this, [this] {
        saveCurrentBreakpoints();
        updateBytecodeDebuggerPreview();
    }, Qt::QueuedConnection);
}

void MainWindow::onWatchExpressionsChanged() {
    QMetaObject::invokeMethod(this, [this] { refreshDebugWatchTable(); }, Qt::QueuedConnection);
}

void MainWindow::openInitialPath(const QString& path) {
    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::showStartScreen() {
    StartScreenDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        openInitialPath(dialog.selectedPath());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (handlePaintMouseEvent(watched, event)) {
        return true;
    }
    if (handleStageMouseEvent(watched, event)) {
        return true;
    }
    if (handleStageKeyEvent(watched, event)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createMenus() {
    const auto menuText = mainMenuText();

    auto* file = menuBar()->addMenu(toQString(menuText.fileMenuText));
    for (const auto& command : fileCommandSpecs()) {
        if (command.id == "exit") {
            continue;
        }
        if (command.id == "open-recent") {
            recentProjectsMenu_ = file->addMenu(toQString(command.menuText));
            connect(recentProjectsMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjectsMenu);
            refreshRecentProjectsMenu();
        } else {
            auto* action = file->addAction(toQString(command.menuText));
            action->setShortcut(shortcutFromSpec(command.shortcut));
            if (command.id == "new-movie") {
                connect(action, &QAction::triggered, this, &MainWindow::createNewMovie);
            } else if (command.id == "new-cast") {
                connect(action, &QAction::triggered, this, &MainWindow::createNewCast);
            } else if (command.id == "open") {
                connect(action, &QAction::triggered, this, &MainWindow::openFileDialog);
            } else if (command.id == "close") {
                connect(action, &QAction::triggered, &context_, &EditorContext::closeFile);
            } else if (command.id == "save") {
                connect(action, &QAction::triggered, this, &MainWindow::saveMovie);
            } else if (command.id == "save-as") {
                connect(action, &QAction::triggered, this, &MainWindow::saveMovieAs);
            } else if (command.id == "save-all") {
                connect(action, &QAction::triggered, this, &MainWindow::saveAllMovies);
            } else if (command.id == "import") {
                connect(action, &QAction::triggered, this, &MainWindow::importMedia);
            } else if (command.id == "export") {
                connect(action, &QAction::triggered, this, &MainWindow::exportSelectedCastMembers);
            }
        }
        if (command.id == "new-cast" || command.id == "close" || command.id == "save-all" ||
            command.id == "export") {
            addSeparator(file);
        }
    }
    auto* preferences = file->addMenu(toQString(menuText.preferencesMenuText));
    for (const auto& panel : preferencePanelSpecs()) {
        preferences->addAction(toQString(panel.menuText), this, [this, panelId = toQString(panel.id)] {
            if (panelId == QStringLiteral("general")) {
                editGeneralPreferences();
            } else {
                editPreferencePanel(panelId);
            }
        });
    }
    addSeparator(file);
    for (const auto& command : fileCommandSpecs()) {
        if (command.id == "exit") {
            auto* action = file->addAction(toQString(command.menuText));
            action->setShortcut(shortcutFromSpec(command.shortcut));
            connect(action, &QAction::triggered, qApp, &QApplication::quit);
            break;
        }
    }

    auto* edit = menuBar()->addMenu(toQString(menuText.editMenuText));
    for (const auto& command : editCommandSpecs()) {
        if (command.id == "find") {
            auto* find = edit->addMenu(toQString(command.menuText));
            for (const auto& findCommand : findCommandSpecs()) {
                auto* action = find->addAction(toQString(findCommand.menuText));
                action->setShortcut(shortcutFromSpec(findCommand.shortcut));
                if (findCommand.id == "find") {
                    connect(action, &QAction::triggered, this, &MainWindow::findInFocusedWidget);
                } else if (findCommand.id == "find-again") {
                    connect(action, &QAction::triggered, this, &MainWindow::findAgainInFocusedWidget);
                } else if (findCommand.id == "replace") {
                    connect(action, &QAction::triggered, this, &MainWindow::replaceInFocusedWidget);
                } else if (findCommand.id == "find-selection") {
                    connect(action, &QAction::triggered, this, &MainWindow::findSelectionInFocusedWidget);
                }
            }
        } else {
            auto* action = edit->addAction(toQString(command.menuText));
            action->setShortcut(shortcutFromSpec(command.shortcut));
            if (command.id == "undo") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("undo", toQString(editCommandSuccessStatusText("undo")));
                });
            } else if (command.id == "redo") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("redo", toQString(editCommandSuccessStatusText("redo")));
                });
            } else if (command.id == "cut") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("cut", toQString(editCommandSuccessStatusText("cut")));
                });
            } else if (command.id == "copy") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("copy", toQString(editCommandSuccessStatusText("copy")));
                });
            } else if (command.id == "paste") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("paste", toQString(editCommandSuccessStatusText("paste")));
                });
            } else if (command.id == "clear") {
                connect(action, &QAction::triggered, this, &MainWindow::clearFocusedSelection);
            } else if (command.id == "select-all") {
                connect(action, &QAction::triggered, this, [this] {
                    performFocusedEditAction("selectAll", toQString(editCommandSuccessStatusText("select-all")));
                });
            } else if (command.id == "edit-sprite-frames") {
                connect(action, &QAction::triggered, this, &MainWindow::editSelectedSpriteFrames);
            } else if (command.id == "edit-entire-sprite") {
                connect(action, &QAction::triggered, this, &MainWindow::editSelectedEntireSprite);
            } else if (command.id == "exchange-cast-members") {
                connect(action, &QAction::triggered, this, &MainWindow::showExchangeCastMembersDialog);
            }
        }
        if (command.id == "redo" || command.id == "select-all" || command.id == "find" ||
            command.id == "edit-entire-sprite") {
            addSeparator(edit);
        }
    }

    auto* view = menuBar()->addMenu(toQString(menuText.viewMenuText));
    auto* zoom = view->addMenu(toQString(menuText.zoomMenuText));
    auto* zoomGroup = new QActionGroup(this);
    zoomGroup->setExclusive(true);
    for (const auto percent : viewZoomPercentages()) {
        auto* action = zoom->addAction(toQString(viewZoomMenuItemText(percent)));
        action->setCheckable(true);
        action->setData(percent);
        zoomGroup->addAction(action);
        stageZoomActions_.push_back(action);
        connect(action, &QAction::triggered, this, [this, percent] { setStageZoom(percent); });
    }
    addSeparator(view);
    auto* overlay = view->addMenu(toQString(menuText.spriteOverlayMenuText));
    for (const auto& command : viewSpriteOverlaySpecs()) {
        auto* action = overlay->addAction(toQString(command.menuText));
        action->setCheckable(command.checkable);
        if (command.id == "sprite-overlay-info") {
            spriteOverlayInfoAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) {
                setSpriteOverlayInfoVisible(visible);
            });
        } else if (command.id == "sprite-overlay-paths") {
            spriteOverlayPathsAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) {
                setSpriteOverlayPathsVisible(visible);
            });
        }
    }
    addSeparator(view);
    for (const auto& command : viewTopLevelToggleSpecs()) {
        auto* action = view->addAction(toQString(command.menuText));
        action->setCheckable(command.checkable);
        if (command.id == "sprite-toolbar") {
            spriteToolbarAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) { setSpriteToolbarVisible(visible); });
        } else if (command.id == "keyframes") {
            scoreKeyframesAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) { setScoreKeyframesVisible(visible); });
        }
    }
    addSeparator(view);
    auto* grids = view->addMenu(toQString(menuText.gridsMenuText));
    for (const auto& command : viewGridSpecs()) {
        auto* action = grids->addAction(toQString(command.menuText));
        action->setCheckable(command.checkable);
        if (command.id == "score-grid-lines") {
            scoreGridLinesAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) { setScoreGridLinesVisible(visible); });
        } else if (command.id == "stage-grid-snap") {
            stageGridSnapAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool enabled) { setStageGridSnapEnabled(enabled); });
        } else if (command.id == "grid-settings") {
            connect(action, &QAction::triggered, this, [this] { showGridSettingsDialog(); });
        }
    }
    auto* guides = view->addMenu(toQString(menuText.guidesMenuText));
    for (const auto& command : viewGuideSpecs()) {
        auto* action = guides->addAction(toQString(command.menuText));
        action->setCheckable(command.checkable);
        if (command.id == "stage-guides-show") {
            stageGuidesShowAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool visible) { setStageGuidesVisible(visible); });
        } else if (command.id == "stage-guides-snap") {
            stageGuidesSnapAction_ = action;
            connect(action, &QAction::toggled, this, [this](bool enabled) { setStageGuidesSnapEnabled(enabled); });
        }
    }

    auto* insert = menuBar()->addMenu(toQString(menuText.insertMenuText));
    for (const auto& command : insertCommandSpecs()) {
        auto* action = insert->addAction(toQString(command.menuText));
        action->setShortcut(shortcutFromSpec(command.shortcut));
        if (command.id == "keyframe") {
            connect(action, &QAction::triggered, this, [this] { insertKeyframe(); });
        } else if (command.id == "marker") {
            connect(action, &QAction::triggered, this, [this] { insertMarker(); });
        } else if (command.id == "remove-frame") {
            connect(action, &QAction::triggered, this, [this] { removeCurrentFrame(); });
        }
        if (command.id == "marker") {
            addSeparator(insert);
        }
    }
    addSeparator(insert);
    auto* mediaElement = insert->addMenu(toQString(menuText.mediaElementMenuText));
    for (const auto& item : mediaElementSpecs()) {
        mediaElement->addAction(toQString(item.menuText), this, [this, mediaType = toQString(item.menuText)] {
            insertMediaElement(mediaType);
        });
    }

    auto* modify = menuBar()->addMenu(toQString(menuText.modifyMenuText));
    QMap<QString, QMenu*> modifyGroups;
    QString previousModifyGroup;
    for (const auto& command : modifyCommandSpecs()) {
        const QString groupName = toQString(command.group);
        if (groupName != previousModifyGroup && !previousModifyGroup.isEmpty()) {
            addSeparator(modify);
        }
        previousModifyGroup = groupName;

        QMenu* targetMenu = modify;
        if (!groupName.isEmpty()) {
            targetMenu = modifyGroups.value(groupName, nullptr);
            if (!targetMenu) {
                targetMenu = modify->addMenu(groupName);
                modifyGroups.insert(groupName, targetMenu);
            }
        }
        auto* action = targetMenu->addAction(toQString(command.menuText));
        action->setShortcut(shortcutFromSpec(command.shortcut));
        if (command.id == "movie-properties") {
            connect(action, &QAction::triggered, this, &MainWindow::showMoviePropertiesDialog);
        } else if (command.id == "movie-casts") {
            connect(action, &QAction::triggered, this, &MainWindow::showMovieCastsDialog);
        } else if (command.id == "external-parameters") {
            connect(action, &QAction::triggered, this, &MainWindow::editExternalParams);
        } else if (command.id == "sprite-properties") {
            connect(action, &QAction::triggered, this, &MainWindow::showSpritePropertiesDialog);
        } else if (command.id == "sprite-tweening") {
            connect(action, &QAction::triggered, this, &MainWindow::showSpriteTweeningDialog);
        } else if (command.id == "cast-member-properties") {
            connect(action, &QAction::triggered, this, &MainWindow::showCastMemberPropertiesDialog);
        } else if (command.id == "frame-tempo") {
            connect(action, &QAction::triggered, this, [this] { showFrameChannelDialog(QStringLiteral("Tempo")); });
        } else if (command.id == "frame-palette") {
            connect(action, &QAction::triggered, this, [this] { showFrameChannelDialog(QStringLiteral("Palette")); });
        } else if (command.id == "frame-transition") {
            connect(action, &QAction::triggered, this, [this] { showFrameChannelDialog(QStringLiteral("Transition")); });
        } else if (command.id == "frame-sound") {
            connect(action, &QAction::triggered, this, [this] { showFrameChannelDialog(QStringLiteral("Sound")); });
        } else if (command.id == "font") {
            connect(action, &QAction::triggered, this, &MainWindow::showTextFontDialog);
        } else if (command.id == "paragraph") {
            connect(action, &QAction::triggered, this, &MainWindow::showTextParagraphDialog);
        }
    }

    auto* control = menuBar()->addMenu(toQString(menuText.controlMenuText));
    const auto playbackCommands = playbackCommandSpecs();
    for (std::size_t i = 0; i < playbackCommands.size(); ++i) {
        if (i == 3) {
            addSeparator(control);
        }
        const auto& command = playbackCommands[i];
        auto* action = control->addAction(toQString(command.menuText));
        action->setShortcut(playbackCommandShortcut(command.shortcut));
        connectPlaybackCommand(action, context_, command.id);
    }
    addSeparator(control);
    for (const auto& command : controlToggleSpecs()) {
        auto* action = control->addAction(toQString(command.menuText));
        action->setCheckable(true);
        if (command.id == "loop-playback") {
            action->setChecked(context_.loopPlayback());
            connect(action, &QAction::toggled, &context_, &EditorContext::setLoopPlayback);
        } else {
            action->setChecked(command.checkedByDefault);
        }
    }

    auto* debug = menuBar()->addMenu(toQString(menuText.debugMenuText));
    const auto debugCommands = debugCommandSpecs();
    for (std::size_t i = 0; i < debugCommands.size(); ++i) {
        if (i == 4 || i == 6) {
            addSeparator(debug);
        }
        const auto& command = debugCommands[i];
        if (command.id == "detailed-stack") {
            auto* detailedStack = debug->addAction(toQString(command.menuText));
            detailedStack->setObjectName(QStringLiteral("DetailedStackWindowAction"));
            detailedStack->setCheckable(true);
            detailedStack->setShortcut(debugCommandShortcut(command.shortcut));
            connect(detailedStack, &QAction::toggled, this, [this](bool visible) { showDetailedStackWindow(visible); });
            continue;
        }
        if (command.id == "trace-handler") {
            auto* traceHandler = debug->addAction(toQString(command.menuText));
            traceHandler->setShortcut(debugCommandShortcut(command.shortcut));
            connect(traceHandler, &QAction::triggered, this, [this] { showTraceHandlerDialog(); });
            continue;
        }
        auto* action = debug->addAction(toQString(command.menuText));
        action->setShortcut(debugCommandShortcut(command.shortcut));
        connect(action, &QAction::triggered, this, [this, command] { triggerDebugCommand(command.id); });
    }

    auto* window = menuBar()->addMenu(toQString(menuText.windowMenuText));
    for (const auto& spec : editorPanelSpecs()) {
        auto* action = window->addAction(toQString(spec.title));
        action->setCheckable(true);
        action->setShortcut(shortcutFromSpec(spec.shortcut));
        panelActions_.insert(toQString(spec.id), action);
    }
    addSeparator(window);
    for (const auto& command : windowCommandSpecs()) {
        if (command.id == "reset-layout") {
            window->addAction(toQString(command.menuText), this, [this] { resetLayout(); });
        }
    }

    auto* help = menuBar()->addMenu(toQString(menuText.helpMenuText));
    for (const auto& command : helpCommandSpecs()) {
        auto* action = help->addAction(toQString(command.menuText));
        if (command.id == "about") {
            connect(action, &QAction::triggered, this, [this, command] {
                QMessageBox::about(this, toQString(command.dialogTitle), toQString(command.dialogText));
            });
        }
    }
}

void MainWindow::createToolbar() {
    const auto toolbarText = playbackToolbarText();
    auto* toolbar = addToolBar(toQString(toolbarText.title));
    toolbar->setObjectName(QStringLiteral("PlaybackToolbar"));
    toolbar->setMovable(false);
    auto* appStyle = QApplication::style();
    for (const auto& command : playbackCommandSpecs()) {
        if (command.id == "step-forward") {
            toolbar->addSeparator();
        }
        auto* action = toolbar->addAction(playbackCommandIcon(*appStyle, command.id), toQString(command.toolbarText));
        action->setToolTip(toQString(command.toolbarText));
        connectPlaybackCommand(action, context_, command.id);
    }
    toolbar->addSeparator();
    frameLabel_ = new QLabel(toQString(toolbarText.initialFrameText), toolbar);
    toolbar->addWidget(frameLabel_);
}

void MainWindow::createPanels() {
    for (const auto& spec : editorPanelSpecs()) {
        QWidget* content = nullptr;
        const QString id = toQString(spec.id);
        if (id == QStringLiteral("stage")) {
            content = makeStagePanel();
        } else if (id == QStringLiteral("score")) {
            content = makeScorePanel();
        } else if (id == QStringLiteral("cast")) {
            content = makeCastPanel();
        } else if (id == QStringLiteral("property-inspector")) {
            content = makePropertyInspectorPanel();
        } else if (id == QStringLiteral("script")) {
            content = makeScriptPanel();
        } else if (id == QStringLiteral("message")) {
            content = makeMessagePanel();
        } else if (id == QStringLiteral("tool-palette")) {
            content = makeToolPalettePanel();
        } else if (id == QStringLiteral("paint")) {
            content = makePaintPanel();
        } else if (id == QStringLiteral("text")) {
            content = makeTextEditorPanel();
        } else if (id == QStringLiteral("vector-shape")) {
            content = makeVectorShapePanel();
        } else if (id == QStringLiteral("field")) {
            content = makeFieldEditorPanel();
        } else if (id == QStringLiteral("sound")) {
            content = makeSoundPanel();
        } else if (id == QStringLiteral("color-palettes")) {
            content = makeColorPalettesPanel();
        } else if (id == QStringLiteral("bytecode-debugger")) {
            content = makeBytecodeDebuggerPanel();
        } else {
            content = makePanelContent(toQString(spec.title), {toQString(mainWindowShellText().pendingPanelText)});
        }
        createPanel(spec, content);
    }
}

void MainWindow::createPanel(const EditorPanelSpec& spec, QWidget* content) {
    auto* dock = new QDockWidget(toQString(spec.title), this);
    const QString id = toQString(spec.id);
    dock->setObjectName(id);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWidget(content);
    dock->setContextMenuPolicy(Qt::CustomContextMenu);
    addDockWidget(qtDockArea(spec.defaultDockArea), dock);
    dock->setVisible(spec.visibleByDefault);
    panels_.insert(id, dock);

    connect(dock, &QWidget::customContextMenuRequested, this, [this, dock](const QPoint& pos) {
        QMenu menu(this);
        const auto panelContext = panelContextCommandText();

        auto* showAction = menu.addAction(toQString(dock->isVisible() ? panelContext.raisePanel : panelContext.showPanel));
        connect(showAction, &QAction::triggered, dock, [dock] {
            dock->show();
            dock->raise();
        });

        auto* floatAction = menu.addAction(toQString(dock->isFloating() ? panelContext.dockPanel : panelContext.floatPanel));
        connect(floatAction, &QAction::triggered, dock, [dock] {
            dock->setFloating(!dock->isFloating());
            dock->show();
            dock->raise();
        });

        menu.addSeparator();
        const bool canSplit = !tabifiedDockWidgets(dock).isEmpty();
        for (const auto& command : splitCommandSpecs()) {
            auto* action = menu.addAction(toQString(command.menuText));
            action->setEnabled(canSplit);
            connect(action, &QAction::triggered, this, [this, dock, direction = command.direction] {
                splitPanelFromTabGroup(dock, direction);
            });
        }

        menu.addSeparator();
        for (const auto& command : moveDockCommandSpecs()) {
            auto* action = menu.addAction(toQString(command.menuText));
            connect(action, &QAction::triggered, this, [this, dock, area = command.area] {
                dockPanelToArea(dock, area);
            });
        }

        menu.addSeparator();
        for (const auto& command : dockCommandSpecs()) {
            auto* action = menu.addAction(toQString(command.menuText));
            connect(action, &QAction::triggered, this, [this, dock, area = command.area] {
                dockPanelToArea(dock, area);
            });
        }

        menu.addSeparator();
        auto* closeAction = menu.addAction(toQString(panelContext.closePanel));
        connect(closeAction, &QAction::triggered, dock, &QDockWidget::hide);
        auto* resetAction = menu.addAction(toQString(panelContext.resetLayout));
        connect(resetAction, &QAction::triggered, this, &MainWindow::resetLayout);

        menu.exec(dock->mapToGlobal(pos));
    });

    if (auto found = panelActions_.find(id); found != panelActions_.end()) {
        found.value()->setChecked(spec.visibleByDefault);
        connect(found.value(), &QAction::toggled, dock, &QDockWidget::setVisible);
        connect(dock, &QDockWidget::visibilityChanged, found.value(), &QAction::setChecked);
    }
    if (id == QStringLiteral("tool-palette")) {
        connect(dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            EditorPreferences::setViewOption(QStringLiteral("spriteToolbar"), visible);
            if (spriteToolbarAction_ && spriteToolbarAction_->isChecked() != visible) {
                const bool blocked = spriteToolbarAction_->blockSignals(true);
                spriteToolbarAction_->setChecked(visible);
                spriteToolbarAction_->blockSignals(blocked);
            }
        });
    }
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) { saveLayout(); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) { saveLayout(); });
    connect(dock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) { saveLayout(); });
}

void MainWindow::connectContext() {
    connect(&context_, &EditorContext::movieChanged, this, [this] {
        updateWindowTitle();
        castThumbnailCache_.clear();
        pendingScoreMarkers_.clear();
        pendingScoreKeyframes_.clear();
        pendingRemovedFrames_.clear();
        pendingBehaviorChanges_.clear();
        stagePlayer_.reset();
        if (const auto movie = context_.movie()) {
            stagePlayer_ = std::make_unique<::libreshockwave::player::Player>(movie);
            configureStagePlayerLocalHttpRoot();
            stagePlayer_->setExternalCastLoadListener([this](const auto&) {
                QMetaObject::invokeMethod(this, [this] {
                    castThumbnailCache_.clear();
                    updateMovieViews();
                    refreshStageFrame();
                }, Qt::QueuedConnection);
            });
            attachDebugControllerToStagePlayer();
            syncExternalParamsToStagePlayer();
            (void)stagePlayer_->preloadAllCasts();
        }
        updateMovieViews();
    });
    connect(&context_, &EditorContext::frameChanged, this, [this](int frame) { updateFrameLabel(frame); });
    connect(&context_, &EditorContext::externalParamsChanged, this, [this] {
        syncExternalParamsToStagePlayer();
        refreshStageFrame();
    });
    connect(&context_, &EditorContext::playbackChanged, this, [this](bool playing) {
        if (!playbackTimer_) {
            return;
        }
        if (playing) {
            playbackTimer_->start(playbackIntervalMs(context_.movie()));
        } else {
            playbackTimer_->stop();
        }
    });
    connect(&context_, &EditorContext::selectionChanged, this, [this](const SelectionState& selection) {
        updatePropertyInspectorForSelection(selection);
        syncSelectionToPanels(selection);
    });
    connect(&context_, &EditorContext::statusMessageChanged, this, [this](const QString& message) {
        statusLabel_->setText(message);
    });
}

void MainWindow::createNewMovie() {
    const auto creationText = fileCreationText();
    QDialog dialog(this);
    dialog.setWindowTitle(toQString(creationText.newMovieTitle));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    auto* stageSize = new QComboBox(&dialog);
    for (const auto choice : fileCreationStageSizeChoices()) {
        stageSize->addItem(toQString(choice));
    }
    form->addRow(toQString(creationText.stageSizeLabel), stageSize);

    auto* frameCount = new QSpinBox(&dialog);
    frameCount->setRange(1, 30000);
    frameCount->setValue(1);
    form->addRow(toQString(creationText.framesLabel), frameCount);

    auto* channels = new QSpinBox(&dialog);
    channels->setRange(1, 1000);
    channels->setValue(48);
    form->addRow(toQString(creationText.spriteChannelsLabel), channels);
    root->addLayout(form);

    auto* note = new QLabel(toQString(creationText.newMoviePendingText), &dialog);
    note->setWordWrap(true);
    root->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    context_.closeFile();
    if (statusLabel_) {
        statusLabel_->setText(toQString(fileCreationMovieStatusText(stageSize->currentText().toStdString(),
                                                                    frameCount->value(),
                                                                    channels->value())));
    }
}

void MainWindow::createNewCast() {
    const auto creationText = fileCreationText();
    QDialog dialog(this);
    dialog.setWindowTitle(toQString(creationText.newCastTitle));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    auto* name = new QLineEdit(toQString(creationText.newCastTitle), &dialog);
    form->addRow(toQString(creationText.castNameLabel), name);
    auto* type = new QComboBox(&dialog);
    type->addItem(toQString(creationText.internalCastType));
    type->addItem(toQString(creationText.externalCastType));
    form->addRow(toQString(creationText.castTypeLabel), type);
    root->addLayout(form);

    auto* note = new QLabel(toQString(creationText.newCastPendingText), &dialog);
    note->setWordWrap(true);
    root->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    showPanel(QStringLiteral("cast"));
    if (statusLabel_) {
        statusLabel_->setText(toQString(fileCreationCastStatusText(type->currentText().toStdString(),
                                                                   name->text().trimmed().toStdString())));
    }
}

void MainWindow::saveMovie() {
    const auto saveText = fileSaveText();
    if (!context_.movie()) {
        QMessageBox::information(this, toQString(saveText.saveTitle), toQString(saveText.noMovieText));
        return;
    }

    const QString path = context_.currentPath().empty()
                             ? toQString(saveText.newMoviePathText)
                             : QString::fromStdString(context_.currentPath().string());
    QMessageBox::information(this,
                             toQString(saveText.saveTitle),
                             toQString(fileSavePreparedDialogText(path.toStdString())));
    if (statusLabel_) {
        const QString displayName = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
        statusLabel_->setText(toQString(fileSavePreparedStatusText(displayName.toStdString())));
    }
}

void MainWindow::saveMovieAs() {
    const auto saveText = fileSaveText();
    if (!context_.movie()) {
        QMessageBox::information(this, toQString(saveText.saveAsTitle), toQString(saveText.noMovieText));
        return;
    }

    const QString defaultPath = context_.currentPath().empty()
                                    ? EditorPreferences::lastOpenDirectory()
                                    : QString::fromStdString(context_.currentPath().string());
    const QString path = QFileDialog::getSaveFileName(
        this,
        toQString(saveText.saveAsDialogTitle),
        defaultPath,
        toQString(saveText.directorFileFilter));
    if (path.isEmpty()) {
        return;
    }

    EditorPreferences::setLastOpenDirectory(QFileInfo(path).absolutePath());
    QMessageBox::information(this,
                             toQString(saveText.saveAsTitle),
                             toQString(fileSaveAsPreparedDialogText(path.toStdString())));
    if (statusLabel_) {
        statusLabel_->setText(toQString(fileSaveAsPreparedStatusText(QFileInfo(path).fileName().toStdString())));
    }
}

void MainWindow::saveAllMovies() {
    const auto saveText = fileSaveText();
    if (!context_.movie()) {
        QMessageBox::information(this, toQString(saveText.saveAllTitle), toQString(saveText.saveAllNoChangesText));
        return;
    }

    QMessageBox::information(this,
                             toQString(saveText.saveAllTitle),
                             toQString(fileSaveAllPreparedDialogText()));
    if (statusLabel_) {
        statusLabel_->setText(toQString(saveText.saveAllStatusText));
    }
}

void MainWindow::openFileDialog() {
    const auto fileText = fileOpenImportText();
    const QString path = QFileDialog::getOpenFileName(
        this,
        toQString(fileText.openDirectorFileTitle),
        EditorPreferences::lastOpenDirectory(),
        toQString(fileText.directorFileFilter));
    if (!path.isEmpty()) {
        EditorPreferences::setLastOpenDirectory(QFileInfo(path).absolutePath());
        openPath(path);
    }
}

void MainWindow::openPath(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    std::string error;
    if (!context_.openFile(std::filesystem::path(absolutePath.toStdString()), &error)) {
        QMessageBox::critical(this, toQString(fileOpenImportText().openFailedTitle), QString::fromStdString(error));
        return;
    }
    EditorPreferences::addRecentProject(absolutePath);
    refreshRecentProjectsMenu();
    const bool hasSavedParams = EditorPreferences::hasMovieParams(absolutePath);
    const bool hasLocalHttpRoot = !localHttpRootForMoviePath(std::filesystem::path(absolutePath.toStdString())).empty();
    context_.loadExternalParams(hasSavedParams ? EditorPreferences::movieParams(absolutePath)
                                               : (hasLocalHttpRoot ? habboPresetParams() : QMap<QString, QString>{}));
}

void MainWindow::refreshRecentProjectsMenu() {
    if (!recentProjectsMenu_) {
        return;
    }

    recentProjectsMenu_->clear();
    const auto recentText = recentProjectsMenuText();
    const auto recentProjects = EditorPreferences::recentProjects();
    if (recentProjects.isEmpty()) {
        auto* empty = recentProjectsMenu_->addAction(toQString(recentText.emptyText));
        empty->setEnabled(false);
        return;
    }

    for (const auto& path : recentProjects) {
        const QFileInfo info(path);
        auto* action = recentProjectsMenu_->addAction(info.fileName().isEmpty() ? path : info.fileName());
        action->setToolTip(path);
        action->setData(path);
        action->setEnabled(info.exists());
        connect(action, &QAction::triggered, this, [this, path, info] {
            if (!info.exists()) {
                const auto recentText = recentProjectsMenuText();
                QMessageBox::warning(this,
                                     toQString(recentText.missingWarningTitle),
                                     toQString(recentText.missingWarningMessagePrefix) + path);
                return;
            }
            EditorPreferences::setLastOpenDirectory(info.absolutePath());
            openPath(path);
        });
    }

    recentProjectsMenu_->addSeparator();
    auto* clearRecent = recentProjectsMenu_->addAction(toQString(recentText.clearText));
    connect(clearRecent, &QAction::triggered, this, [this] {
        EditorPreferences::clearRecentProjects();
        refreshRecentProjectsMenu();
        if (statusLabel_) {
            statusLabel_->setText(toQString(recentProjectsMenuText().clearedStatusText));
        }
    });
}

void MainWindow::importMedia() {
    const auto fileText = fileOpenImportText();
    if (!context_.movie()) {
        QMessageBox::information(this, toQString(fileText.importTitle), toQString(fileText.importNoMovieText));
        return;
    }

    const int selectedCastLib = castSelector_ ? castSelector_->currentData().toInt() : 0;
    if (stagePlayer_ && selectedCastLib > 0) {
        if (auto castLibrary = stagePlayer_->castLibManager().getCastLib(selectedCastLib);
            castLibrary && castLibrary->isExternal()) {
            loadSelectedExternalCast();
            return;
        }
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        toQString(fileText.importMediaTitle),
        EditorPreferences::lastOpenDirectory(),
        toQString(fileText.importMediaFilter));
    if (path.isEmpty()) {
        return;
    }

    EditorPreferences::setLastOpenDirectory(QFileInfo(path).absolutePath());
    const auto memberRefs = selectedCastMemberRefs();
    auto importBitmapIntoMember = [this, &path](int runtimeCastLib, int memberNum) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            return false;
        }

        const QByteArray bytes = input.readAll();
        std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes.size()));
        std::copy(bytes.cbegin(), bytes.cend(), data.begin());
        stagePlayer_->castLibManager().cacheExternalData(path.toStdString(), data);
        return stagePlayer_->castLibManager().importFileIntoMember(runtimeCastLib,
                                                                   memberNum,
                                                                   path.toStdString(),
                                                                   lingo::Datum::voidValue());
    };

    if (stagePlayer_ && memberRefs.size() == 1) {
        const auto target = memberRefs.front();
        const int runtimeCastLib = target.castLib > 0 ? target.castLib : 1;
        const bool imported = importBitmapIntoMember(runtimeCastLib, target.memberNum);

        updateMovieViews();
        context_.selectCastMember(target.castLib, target.memberNum);
        syncCastSelectionToTable(context_.selection());
        refreshStageFrame();
        showPanel(QStringLiteral("cast"));
        if (imported) {
            loadCastMemberIntoPanel(QStringLiteral("paint"), {}, target.castLib, target.memberNum);
        }
        if (statusLabel_) {
            const auto displayName = QFileInfo(path).fileName().toStdString();
            statusLabel_->setText(toQString(imported ? fileImportAppliedStatusText(displayName, target.memberNum)
                                                     : fileImportFailedStatusText(displayName, target.memberNum)));
        }
        return;
    }

    if (stagePlayer_ && memberRefs.isEmpty()) {
        const int runtimeCastLib = selectedCastLib > 0 ? selectedCastLib : 1;
        const auto displayName = QFileInfo(path).fileName().toStdString();
        const auto created = stagePlayer_->castLibManager().createMember(runtimeCastLib, "bitmap");
        const auto* ref = created.asCastMemberRef();
        if (ref != nullptr) {
            const int uiCastLib = ref->castLib == 1 ? 0 : ref->castLib;
            const int memberNum = ref->memberNum();
            std::string memberName = QFileInfo(path).completeBaseName().toStdString();
            if (memberName.empty()) {
                memberName = displayName.empty() ? "Imported Bitmap" : displayName;
            }
            (void)stagePlayer_->castLibManager().setMemberProp(ref->castLib,
                                                               memberNum,
                                                               "name",
                                                               lingo::Datum::of(memberName));
            const bool imported = importBitmapIntoMember(ref->castLib, memberNum);
            if (!imported) {
                (void)stagePlayer_->castLibManager().eraseMember(ref->castLib, memberNum);
            }

            updateMovieViews();
            showPanel(QStringLiteral("cast"));
            if (imported) {
                context_.selectCastMember(uiCastLib, memberNum);
                syncCastSelectionToTable(context_.selection());
                refreshStageFrame();
                loadCastMemberIntoPanel(QStringLiteral("paint"), {}, uiCastLib, memberNum);
            }
            if (statusLabel_) {
                statusLabel_->setText(toQString(imported ? fileImportCreatedStatusText(displayName, memberNum)
                                                         : fileImportCreateFailedStatusText(displayName)));
            }
            return;
        }

        updateMovieViews();
        showPanel(QStringLiteral("cast"));
        if (statusLabel_) {
            statusLabel_->setText(toQString(fileImportCreateFailedStatusText(displayName)));
        }
        return;
    }

    showPanel(QStringLiteral("cast"));
    QMessageBox::information(this,
                             toQString(fileText.importMediaTitle),
                             toQString(fileImportPreparedDialogText(path.toStdString())));
    if (statusLabel_) {
        statusLabel_->setText(toQString(fileImportPreparedStatusText(QFileInfo(path).fileName().toStdString())));
    }
}

void MainWindow::editGeneralPreferences() {
    const auto preferencesText = generalPreferencesText();
    QDialog dialog(this);
    dialog.setWindowTitle(toQString(preferencesText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    auto* lastOpenDirectory = new QLineEdit(EditorPreferences::lastOpenDirectory(), &dialog);
    auto* browseRow = new QWidget(&dialog);
    auto* browseLayout = new QHBoxLayout(browseRow);
    browseLayout->setContentsMargins(0, 0, 0, 0);
    browseLayout->setSpacing(6);
    auto* browse = new QPushButton(toQString(preferencesText.browseText), browseRow);
    browseLayout->addWidget(lastOpenDirectory, 1);
    browseLayout->addWidget(browse);
    form->addRow(toQString(preferencesText.defaultOpenFolderLabel), browseRow);
    root->addLayout(form);

    auto* recentLabel = new QLabel(toQString(preferencesText.recentProjectsLabel), &dialog);
    root->addWidget(recentLabel);
    auto* recentList = new QListWidget(&dialog);
    recentList->setSelectionMode(QAbstractItemView::NoSelection);
    recentList->setMinimumHeight(120);
    const auto refreshRecentList = [recentList, preferencesText](const QStringList& projects) {
        recentList->clear();
        if (projects.isEmpty()) {
            auto* item = new QListWidgetItem(toQString(preferencesText.emptyRecentProjectsText));
            item->setFlags(Qt::NoItemFlags);
            recentList->addItem(item);
            return;
        }
        for (const auto& path : projects) {
            auto* item = new QListWidgetItem(path);
            item->setFlags(Qt::NoItemFlags);
            recentList->addItem(item);
        }
    };
    refreshRecentList(EditorPreferences::recentProjects());
    root->addWidget(recentList);

    bool clearRecentProjects = false;
    auto* clearRecent = new QPushButton(toQString(preferencesText.clearRecentProjectsText), &dialog);
    root->addWidget(clearRecent, 0, Qt::AlignLeft);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, &dialog, [&dialog, lastOpenDirectory, preferencesText] {
        const QString path = QFileDialog::getExistingDirectory(
            &dialog,
            toQString(preferencesText.chooseDefaultOpenFolderTitle),
            lastOpenDirectory->text());
        if (!path.isEmpty()) {
            lastOpenDirectory->setText(QDir::toNativeSeparators(path));
        }
    });
    connect(clearRecent, &QPushButton::clicked, &dialog, [&clearRecentProjects, refreshRecentList] {
        clearRecentProjects = true;
        refreshRecentList({});
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    EditorPreferences::setLastOpenDirectory(QDir::fromNativeSeparators(lastOpenDirectory->text()));
    if (clearRecentProjects) {
        EditorPreferences::clearRecentProjects();
        refreshRecentProjectsMenu();
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(preferencesText.savedStatusText));
    }
}

void MainWindow::editPreferencePanel(const QString& panelName) {
    const auto panelText = preferenceCategoryPanelText(panelName.toStdString());
    if (!panelText) {
        return;
    }
    const QString title = toQString(panelText->title);

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    QList<QCheckBox*> boolWidgets;
    QList<QSpinBox*> intWidgets;
    QList<QComboBox*> choiceWidgets;

    for (const auto& option : panelText->boolOptions) {
        auto* box = new QCheckBox(toQString(option.label), &dialog);
        box->setChecked(EditorPreferences::preferenceBool(panelName, toQString(option.key), option.defaultValue));
        boolWidgets.push_back(box);
        root->addWidget(box);
    }

    for (const auto& option : panelText->intOptions) {
        auto* spin = new QSpinBox(&dialog);
        spin->setRange(option.minimum, option.maximum);
        spin->setValue(EditorPreferences::preferenceInt(panelName, toQString(option.key), option.defaultValue));
        intWidgets.push_back(spin);
        form->addRow(toQString(option.label), spin);
    }

    for (const auto& option : panelText->choiceOptions) {
        auto* combo = new QComboBox(&dialog);
        for (const auto value : option.values) {
            combo->addItem(toQString(value));
        }
        const QString defaultValue = toQString(option.defaultValue);
        const QString saved = EditorPreferences::preferenceString(panelName, toQString(option.key), defaultValue);
        const int index = combo->findText(saved);
        combo->setCurrentIndex(index >= 0 ? index : combo->findText(defaultValue));
        choiceWidgets.push_back(combo);
        form->addRow(toQString(option.label), combo);
    }

    if (form->rowCount() > 0) {
        root->addLayout(form);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    for (qsizetype i = 0; i < boolWidgets.size(); ++i) {
        EditorPreferences::setPreferenceBool(panelName,
                                             toQString(panelText->boolOptions[static_cast<std::size_t>(i)].key),
                                             boolWidgets[i]->isChecked());
    }
    for (qsizetype i = 0; i < intWidgets.size(); ++i) {
        EditorPreferences::setPreferenceInt(panelName,
                                            toQString(panelText->intOptions[static_cast<std::size_t>(i)].key),
                                            intWidgets[i]->value());
    }
    for (qsizetype i = 0; i < choiceWidgets.size(); ++i) {
        EditorPreferences::setPreferenceString(panelName,
                                               toQString(panelText->choiceOptions[static_cast<std::size_t>(i)].key),
                                               choiceWidgets[i]->currentText());
    }

    if (panelName == QStringLiteral("script")) {
        if (scriptPreview_) {
            const int tabWidth =
                std::clamp(EditorPreferences::preferenceInt(QStringLiteral("script"), QStringLiteral("tabWidth"), 4), 1, 16);
            scriptPreview_->setTabStopDistance(scriptPreview_->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * tabWidth);
        }
        if (scriptLingoToggle_) {
            const QString defaultView =
                EditorPreferences::preferenceString(QStringLiteral("script"), QStringLiteral("defaultView"), QStringLiteral("Lingo"));
            const bool shouldShowLingo = defaultView != QStringLiteral("Bytecode");
            if (scriptLingoToggle_->isChecked() != shouldShowLingo) {
                scriptLingoToggle_->setChecked(shouldShowLingo);
            } else {
                updateScriptEditorPreview();
            }
        } else {
            updateScriptEditorPreview();
        }
        if (debugLingoToggle_) {
            const QString defaultView =
                EditorPreferences::preferenceString(QStringLiteral("script"), QStringLiteral("defaultView"), QStringLiteral("Lingo"));
            const bool shouldShowLingo = defaultView != QStringLiteral("Bytecode");
            if (debugLingoToggle_->isChecked() != shouldShowLingo) {
                debugLingoToggle_->setChecked(shouldShowLingo);
            } else {
                updateBytecodeDebuggerPreview();
            }
        } else {
            updateBytecodeDebuggerPreview();
        }
    } else if (panelName == QStringLiteral("sprite")) {
        setSpriteOverlayInfoVisible(EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("showOverlays"), true));
        setSpriteOverlayPathsVisible(EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("showPaths"), false));
        setStageGridSnapEnabled(EditorPreferences::preferenceBool(QStringLiteral("sprite"), QStringLiteral("snapToGrid"), false));
    } else if (panelName == QStringLiteral("paint")) {
        paintBrushSize_ =
            std::clamp(EditorPreferences::preferenceInt(QStringLiteral("paint"), QStringLiteral("brushSize"), 3), 1, 128);
        paintAntialiasPreview_ =
            EditorPreferences::preferenceBool(QStringLiteral("paint"), QStringLiteral("antialiasPreview"), true);
        paintShowTransparencyGrid_ =
            EditorPreferences::preferenceBool(QStringLiteral("paint"), QStringLiteral("showTransparencyGrid"), true);
        updatePaintPreview();
        if (paintStatus_ && paintOriginalPixmap_.isNull()) {
            const QString selectedTool =
                EditorPreferences::preferenceString(QStringLiteral("paint"), QStringLiteral("selectedTool"), QStringLiteral("Pencil"));
            paintStatus_->setText(toQString(paintWindowToolStatus(selectedTool.toStdString(), false, paintBrushSize_)));
        }
    } else if (panelName == QStringLiteral("network")) {
        configureStagePlayerLocalHttpRoot();
    }

    if (statusLabel_) {
        statusLabel_->setText(toQString(preferenceCategorySavedStatusText(panelText->title)));
    }
}

void MainWindow::editExternalParams() {
    ExternalParamsDialog dialog(context_.externalParams(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto params = dialog.params();
    context_.setExternalParams(params);
    if (!context_.currentPath().empty()) {
        EditorPreferences::setMovieParams(QString::fromStdString(context_.currentPath().string()), params);
    }
}

void MainWindow::showMoviePropertiesDialog() {
    const auto movie = context_.movie();
    const auto moviePropertiesText = moviePropertiesDialogText();
    if (!movie) {
        QMessageBox::information(this,
                                 toQString(moviePropertiesText.title),
                                 toQString(moviePropertiesText.noMovieText));
        return;
    }

    const QString fileName = context_.currentPath().empty()
        ? toQString(moviePropertiesText.untitledMovieText)
        : QString::fromStdString(context_.currentPath().filename().string());
    const auto summary = buildMovieSummary(*movie);
    const auto labels = propertyInspectorMovieLabels();
    const auto values = propertyInspectorMovieValues(summary, fileName.toStdString());

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(moviePropertiesText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(fileName, &dialog);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    root->addWidget(title);

    auto* form = new QFormLayout;
    const std::size_t count = std::min(labels.size(), values.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto* value = new QLineEdit(toQString(values[index]), &dialog);
        value->setReadOnly(true);
        form->addRow(toQString(labels[index]), value);
    }
    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showMovieCastsDialog() {
    const auto movie = context_.movie();
    const auto movieCastsText = movieCastsDialogText();
    if (!movie) {
        QMessageBox::information(this, toQString(movieCastsText.title), toQString(movieCastsText.noMovieText));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(movieCastsText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels(toQStringList(movieCastsDialogTableColumns()));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);

    const auto entries = runtimeCastSelectorEntries(stagePlayer_.get());
    for (const auto& entry : entries) {
        const int row = table->rowCount();
        table->insertRow(row);

        const int castLibNumber = entry.castLibNumber;
        QString kind = castLibNumber <= 0 ? toQString(movieCastsText.internalKindText)
                                          : toQString(movieCastsText.externalKindText);
        QString status = toQString(movieCastsText.loadedStatusText);
        QString members = toQString(movieCastsDialogMemberCountText(movie->castMembers().size()));

        if (castLibNumber > 0) {
            status = toQString(movieCastsText.notLoadedStatusText);
            members = toQString(movieCastsText.unsetText);
            if (stagePlayer_) {
                if (auto castLib = stagePlayer_->castLibManager().getCastLib(castLibNumber)) {
                    status = castLib->isLoaded() ? toQString(movieCastsText.loadedStatusText)
                                                 : toQString(movieCastsText.notLoadedStatusText);
                    if (castLib->isLoaded()) {
                        members = toQString(movieCastsDialogMemberCountText(castLib->memberChunks().size()));
                    }
                }
            }
        }

        table->setItem(row, 0, new QTableWidgetItem(entry.label));
        table->setItem(row, 1, new QTableWidgetItem(kind));
        table->setItem(row, 2, new QTableWidgetItem(status));
        table->setItem(row, 3, new QTableWidgetItem(members));
    }
    table->resizeColumnsToContents();
    root->addWidget(table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showFrameChannelDialog(const QString& channelName) {
    const auto frameText = frameChannelDialogText();
    const std::string channelNameString = channelName.toStdString();
    const auto movie = context_.movie();
    if (!movie || !movie->scoreChunk()) {
        QMessageBox::information(this,
                                 toQString(frameChannelDialogTitle(channelNameString)),
                                 toQString(frameText.noScoreText));
        return;
    }

    const int frame = context_.currentFrame();
    const auto score = movie->scoreChunk();

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(frameChannelDialogTitle(channelNameString)));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    auto addReadOnlyRow = [&dialog, form](const QString& label, const QString& value) {
        auto* field = new QLineEdit(value, &dialog);
        field->setReadOnly(true);
        form->addRow(label, field);
    };

    addReadOnlyRow(toQString(frameText.frameLabel), QString::number(frame));

    if (channelName == QStringLiteral("Tempo")) {
        const int tempo = score->getFrameTempo(frame);
        addReadOnlyRow(toQString(frameText.tempoLabel),
                       tempo > 0 ? QString::number(tempo) : toQString(frameText.unsetText));
        addReadOnlyRow(toQString(frameText.parsedTempoEntriesLabel),
                       QString::number(score->frameData().tempoChannelData.size()));
    } else if (channelName == QStringLiteral("Palette")) {
        const auto palette = score->getFramePalette(frame);
        addReadOnlyRow(toQString(frameText.paletteCastLabel),
                       palette ? QString::number(palette->castLib) : toQString(frameText.unsetText));
        addReadOnlyRow(toQString(frameText.paletteMemberLabel),
                       palette ? QString::number(palette->castMember) : toQString(frameText.unsetText));
        addReadOnlyRow(toQString(frameText.descriptionLabel),
                       palette ? toQString(paletteDescription(palette->castMember - 1)) : toQString(frameText.unsetText));
        addReadOnlyRow(toQString(frameText.parsedPaletteEntriesLabel),
                       QString::number(score->frameData().paletteChannelData.size()));
    } else if (channelName == QStringLiteral("Transition")) {
        QString value = toQString(frameText.unsetText);
        for (const auto& entry : score->frameData().frameChannelData) {
            if (entry.frameIndex.value() + 1 == frame && entry.channelIndex.value() == 2 && !entry.data.isEmpty()) {
                value = toQString(frameChannelCastMemberText(entry.data.castLib, entry.data.castMember));
                break;
            }
        }
        addReadOnlyRow(toQString(frameText.transitionLabel), value);
        addReadOnlyRow(toQString(frameText.nativeChannelLabel), toQString(scoreChannelName(2)));
        addReadOnlyRow(toQString(frameText.parsingStatusLabel), toQString(frameText.readOnlySnapshotText));
    } else {
        QString sound1 = toQString(frameText.unsetText);
        QString sound2 = toQString(frameText.unsetText);
        for (const auto& entry : score->frameData().frameChannelData) {
            if (entry.frameIndex.value() + 1 != frame || entry.data.isEmpty()) {
                continue;
            }
            if (entry.channelIndex.value() == 3) {
                sound1 = toQString(frameChannelCastMemberText(entry.data.castLib, entry.data.castMember));
            } else if (entry.channelIndex.value() == 4) {
                sound2 = toQString(frameChannelCastMemberText(entry.data.castLib, entry.data.castMember));
            }
        }
        addReadOnlyRow(toQString(frameText.sound1Label), sound1);
        addReadOnlyRow(toQString(frameText.sound2Label), sound2);
        addReadOnlyRow(toQString(frameText.nativeChannelsLabel),
                       toQString(frameChannelNativeChannelsText(scoreChannelName(3), scoreChannelName(4))));
        addReadOnlyRow(toQString(frameText.parsingStatusLabel), toQString(frameText.readOnlySnapshotText));
    }

    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showCastMemberPropertiesDialog() {
    const auto memberRefs = selectedCastMemberRefs();
    const auto castMemberText = castMemberPropertiesDialogText();
    if (memberRefs.isEmpty()) {
        QMessageBox::information(this, toQString(castMemberText.title), toQString(castMemberText.selectMemberText));
        return;
    }

    const auto ref = memberRefs.first();
    const auto member = castMemberForCastLib(ref.castLib, ref.memberNum);
    if (!member) {
        QMessageBox::information(this, toQString(castMemberText.title), toQString(castMemberText.memberNotLoadedText));
        return;
    }

    QString castName = toQString(castWindowDefaultCastName());
    if (ref.castLib > 0 && stagePlayer_) {
        if (auto castLib = stagePlayer_->castLibManager().getCastLib(ref.castLib)) {
            castName = toQString(castLib->name());
            if (castName.trimmed().isEmpty()) {
                castName = toQString(castWindowCastLibraryLabel(ref.castLib, {}, false, castLib->isExternal(), castLib->isLoaded()));
            }
        }
    }

    const auto row = castMemberRowFromChunk(ref.memberNum, *member);
    const auto labels = propertyInspectorMemberLabels();
    const auto values = propertyInspectorMemberValues(row, castName.toStdString());

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(castMemberText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(toQString(castMemberPropertiesDialogTitle(row.name, ref.memberNum)), &dialog);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    root->addWidget(title);

    auto* form = new QFormLayout;
    const std::size_t count = std::min(labels.size(), values.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto* value = new QLineEdit(toQString(values[index]), &dialog);
        value->setReadOnly(true);
        form->addRow(toQString(labels[index]), value);
    }
    auto* regPoint = new QLineEdit(toQString(registrationPointText(row.regPointX, row.regPointY)), &dialog);
    regPoint->setReadOnly(true);
    form->addRow(toQString(castMemberText.registrationPointLabel), regPoint);
    auto* script = new QLineEdit(row.scriptId > 0 ? QString::number(row.scriptId) : toQString(castMemberText.unsetText),
                                 &dialog);
    script->setReadOnly(true);
    form->addRow(toQString(castMemberText.scriptLabel), script);
    auto* castLib = new QLineEdit(QString::number(ref.castLib), &dialog);
    castLib->setReadOnly(true);
    form->addRow(toQString(castMemberText.castLibraryLabel), castLib);
    root->addLayout(form);

    auto* details = new QPlainTextEdit(toQString(castMemberDetails(row)), &dialog);
    details->setReadOnly(true);
    details->setMaximumHeight(110);
    details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showSpritePropertiesDialog() {
    const auto movie = context_.movie();
    const auto spriteText = spritePropertiesDialogText();
    if (!movie) {
        QMessageBox::information(this, toQString(spriteText.title), toQString(spriteText.noMovieText));
        return;
    }

    const auto selection = context_.selection();
    const auto sprite = findScoreIntervalForSelection(*movie, selection);
    if (!sprite) {
        QMessageBox::information(this, toQString(spriteText.title), toQString(spriteText.selectSpriteText));
        return;
    }

    auto values = propertyInspectorSpriteValues(*sprite);
    if (stagePlayer_ && selection.frame == stagePlayer_->currentFrame()) {
        if (auto liveSprite = stagePlayer_->stageRenderer().spriteRegistry().get(selection.channel)) {
            if (values.size() >= 12) {
                values[2] = std::to_string(liveSprite->locH());
                values[3] = std::to_string(liveSprite->locV());
                values[4] = std::to_string(liveSprite->width());
                values[5] = std::to_string(liveSprite->height());
                values[6] = std::to_string(liveSprite->ink());
                values[7] = std::to_string(liveSprite->blend());
                values[8] = std::to_string(liveSprite->locZ());
                values[9] = liveSprite->isVisible() ? "true" : "false";
                values[10] = stagePlayer_->spriteProperties().getSpriteProp(selection.channel, "moveableSprite").boolValue()
                    ? "true"
                    : "false";
                values[11] = stagePlayer_->spriteProperties().getSpriteProp(selection.channel, "editableText").boolValue()
                    ? "true"
                    : "false";
            }
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(spriteText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(toQString(spritePropertiesDialogHeading(selection.frame,
                                                                     scoreChannelName(selection.channel))),
                             &dialog);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    root->addWidget(title);

    auto* form = new QFormLayout;
    const auto labels = propertyInspectorSpriteLabels();
    const std::size_t count = std::min(labels.size(), values.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto* value = new QLineEdit(toQString(values[index]), &dialog);
        value->setReadOnly(true);
        form->addRow(toQString(labels[index]), value);
    }
    root->addLayout(form);

    auto* details = new QPlainTextEdit(toQString(scoreIntervalTooltip(*sprite)), &dialog);
    details->setReadOnly(true);
    details->setMaximumHeight(120);
    details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showSpriteTweeningDialog() {
    const auto movie = context_.movie();
    const auto tweeningText = spriteTweeningDialogText();
    if (!movie) {
        QMessageBox::information(this, toQString(tweeningText.title), toQString(tweeningText.noMovieText));
        return;
    }

    const auto selection = context_.selection();
    const auto sprite = findScoreIntervalForSelection(*movie, selection);
    if (!sprite) {
        QMessageBox::information(this, toQString(tweeningText.title), toQString(tweeningText.selectSpriteText));
        return;
    }

    showPanel(QStringLiteral("score"));
    context_.selectSprite(selection.channel, selection.frame);

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(tweeningText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(toQString(spritePropertiesDialogHeading(selection.frame,
                                                                     scoreChannelName(selection.channel))),
                             &dialog);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    root->addWidget(title);

    auto* form = new QFormLayout;
    auto* span = new QLineEdit(toQString(spriteTweeningSpanText(sprite->startFrame, sprite->endFrame)), &dialog);
    span->setReadOnly(true);
    form->addRow(toQString(tweeningText.spanLabel), span);
    auto* member = new QLineEdit(sprite->castMember > 0 ? QString::number(sprite->castMember)
                                                        : toQString(tweeningText.unsetText),
                                 &dialog);
    member->setReadOnly(true);
    form->addRow(toQString(tweeningText.castMemberLabel), member);
    auto* channel = new QLineEdit(toQString(scoreChannelName(sprite->channel)), &dialog);
    channel->setReadOnly(true);
    form->addRow(toQString(tweeningText.channelLabel), channel);
    root->addLayout(form);

    auto* properties = new QGroupBox(toQString(tweeningText.tweenedPropertiesTitle), &dialog);
    auto* propertyLayout = new QGridLayout(properties);
    const auto propertyLabels = spriteTweeningPropertyLabels();
    const auto names = toQStringList(propertyLabels);
    for (int index = 0; index < names.size(); ++index) {
        auto* option = new QCheckBox(names[index], properties);
        option->setChecked(index < 2);
        option->setEnabled(false);
        propertyLayout->addWidget(option, index / 2, index % 2);
    }
    root->addWidget(properties);

    auto* controls = new QGroupBox(toQString(tweeningText.settingsTitle), &dialog);
    auto* controlsLayout = new QFormLayout(controls);
    auto* curvature = new QSpinBox(controls);
    curvature->setRange(0, 100);
    curvature->setValue(0);
    curvature->setEnabled(false);
    controlsLayout->addRow(toQString(tweeningText.curvatureLabel), curvature);
    auto* easeIn = new QSpinBox(controls);
    easeIn->setRange(0, 100);
    easeIn->setValue(0);
    easeIn->setEnabled(false);
    controlsLayout->addRow(toQString(tweeningText.easeInLabel), easeIn);
    auto* easeOut = new QSpinBox(controls);
    easeOut->setRange(0, 100);
    easeOut->setValue(0);
    easeOut->setEnabled(false);
    controlsLayout->addRow(toQString(tweeningText.easeOutLabel), easeOut);
    root->addWidget(controls);

    auto* details = new QPlainTextEdit(toQString(spriteTweeningDetailsText(scoreIntervalTooltip(*sprite))), &dialog);
    details->setReadOnly(true);
    details->setMaximumHeight(130);
    details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    root->addWidget(buttons);

    if (statusLabel_) {
        statusLabel_->setText(toQString(spriteTweeningOpenedStatusText(selection.frame,
                                                                       scoreChannelName(selection.channel))));
    }
    dialog.exec();
}

void MainWindow::showTextFontDialog() {
    const auto dialogText = textFormattingDialogText();
    auto* focusedText = qobject_cast<QTextEdit*>(QApplication::focusWidget());
    auto* focusedField = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
    if (!focusedText && !focusedField && textEditorText_) {
        focusedText = textEditorText_;
    }
    if (!focusedText && !focusedField && fieldText_) {
        focusedField = fieldText_;
    }
    if (!focusedText && !focusedField) {
        QMessageBox::information(this, toQString(dialogText.fontTitle), toQString(dialogText.fontNoEditorText));
        return;
    }

    bool accepted = false;
    const QFont currentFont = focusedText ? focusedText->currentCharFormat().font()
                                          : focusedField->currentCharFormat().font();
    const QFont chosen = QFontDialog::getFont(&accepted, currentFont, this, toQString(dialogText.fontTitle));
    if (!accepted) {
        return;
    }

    QTextCharFormat format;
    format.setFont(chosen);
    if (focusedText) {
        focusedText->mergeCurrentCharFormat(format);
        focusedText->setFocus(Qt::OtherFocusReason);
    } else {
        focusedField->mergeCurrentCharFormat(format);
        focusedField->setFocus(Qt::OtherFocusReason);
    }
    if (focusedText && textEditorStatus_) {
        textEditorStatus_->setText(toQString(dialogText.fontTextStatus));
    } else if (focusedField && fieldStatus_) {
        fieldStatus_->setText(toQString(dialogText.fontFieldStatus));
    }
}

void MainWindow::showTextParagraphDialog() {
    const auto dialogText = textFormattingDialogText();
    if (!textEditorText_) {
        QMessageBox::information(this, toQString(dialogText.paragraphTitle), toQString(dialogText.paragraphNoEditorText));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(dialogText.paragraphTitle));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    const QTextBlockFormat current = textEditorText_->textCursor().blockFormat();
    auto* form = new QFormLayout;

    auto* alignment = new QComboBox(&dialog);
    const std::array<Qt::AlignmentFlag, 4> alignmentValues{{
        Qt::AlignLeft,
        Qt::AlignHCenter,
        Qt::AlignRight,
        Qt::AlignJustify,
    }};
    const auto alignmentLabels = paragraphAlignmentLabels();
    for (std::size_t i = 0; i < alignmentLabels.size() && i < alignmentValues.size(); ++i) {
        alignment->addItem(toQString(alignmentLabels[i]), static_cast<int>(alignmentValues[i]));
    }
    const Qt::Alignment align = current.alignment();
    if (align.testFlag(Qt::AlignHCenter)) {
        alignment->setCurrentIndex(1);
    } else if (align.testFlag(Qt::AlignRight)) {
        alignment->setCurrentIndex(2);
    } else if (align.testFlag(Qt::AlignJustify)) {
        alignment->setCurrentIndex(3);
    }
    form->addRow(toQString(dialogText.alignmentLabel), alignment);

    auto* leftMargin = new QSpinBox(&dialog);
    leftMargin->setRange(0, 720);
    leftMargin->setValue(static_cast<int>(std::lround(current.leftMargin())));
    form->addRow(toQString(dialogText.leftIndentLabel), leftMargin);

    auto* rightMargin = new QSpinBox(&dialog);
    rightMargin->setRange(0, 720);
    rightMargin->setValue(static_cast<int>(std::lround(current.rightMargin())));
    form->addRow(toQString(dialogText.rightIndentLabel), rightMargin);

    auto* firstLine = new QSpinBox(&dialog);
    firstLine->setRange(-720, 720);
    firstLine->setValue(static_cast<int>(std::lround(current.textIndent())));
    form->addRow(toQString(dialogText.firstLineLabel), firstLine);

    auto* spacing = new QSpinBox(&dialog);
    spacing->setRange(0, 200);
    spacing->setValue(static_cast<int>(std::lround(current.lineHeight())));
    form->addRow(toQString(dialogText.lineSpacingLabel), spacing);

    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QTextBlockFormat format;
    format.setAlignment(static_cast<Qt::AlignmentFlag>(alignment->currentData().toInt()));
    format.setLeftMargin(leftMargin->value());
    format.setRightMargin(rightMargin->value());
    format.setTextIndent(firstLine->value());
    if (spacing->value() > 0) {
        format.setLineHeight(spacing->value(), QTextBlockFormat::ProportionalHeight);
    }

    QTextCursor cursor = textEditorText_->textCursor();
    cursor.mergeBlockFormat(format);
    textEditorText_->setTextCursor(cursor);
    textEditorText_->setFocus(Qt::OtherFocusReason);
    if (textEditorStatus_) {
        textEditorStatus_->setText(toQString(dialogText.paragraphStatus));
    }
}

void MainWindow::insertKeyframe() {
    const auto insertText = insertActionText();
    const auto movie = context_.movie();
    if (!movie) {
        QMessageBox::information(this, toQString(insertText.keyframeTitle), toQString(insertText.keyframeNoMovieText));
        return;
    }

    const auto selection = context_.selection();
    if (selection.type != SelectionType::ScoreCell && selection.type != SelectionType::Sprite) {
        showPanel(QStringLiteral("score"));
        QMessageBox::information(this,
                                 toQString(insertText.keyframeTitle),
                                 toQString(insertText.keyframeNoSelectionText));
        return;
    }

    context_.selectSprite(selection.channel, selection.frame);
    showPanel(QStringLiteral("score"));
    const auto duplicate = std::find_if(pendingScoreKeyframes_.cbegin(),
                                        pendingScoreKeyframes_.cend(),
                                        [&selection](const PendingScoreKeyframe& keyframe) {
                                            return keyframe.channel == selection.channel &&
                                                   keyframe.frame == selection.frame;
                                        });
    if (duplicate == pendingScoreKeyframes_.cend()) {
        pendingScoreKeyframes_.push_back(PendingScoreKeyframe{
            .channel = selection.channel,
            .frame = selection.frame,
        });
    }
    updateMovieViews();
    const auto channelName = scoreChannelName(selection.channel);
    QMessageBox::information(this,
                             toQString(insertText.keyframeTitle),
                             toQString(insertKeyframePreparedText(selection.frame, channelName)));
    if (statusLabel_) {
        statusLabel_->setText(toQString(insertKeyframeStatusText(selection.frame, channelName)));
    }
}

void MainWindow::insertMarker() {
    const auto insertText = insertActionText();
    const auto movie = context_.movie();
    if (!movie) {
        QMessageBox::information(this, toQString(insertText.markerTitle), toQString(insertText.markerNoMovieText));
        return;
    }

    bool accepted = false;
    const QString markerName = QInputDialog::getText(this,
                                                    toQString(insertText.markerTitle),
                                                    toQString(insertText.markerPromptLabel),
                                                    QLineEdit::Normal,
                                                    toQString(insertText.markerDefaultName),
                                                    &accepted);
    if (!accepted) {
        return;
    }

    const int frame = context_.currentFrame();
    const QString trimmedMarkerName = markerName.trimmed();
    const QString effectiveMarkerName = trimmedMarkerName.isEmpty() ? toQString(insertText.markerDefaultName)
                                                                    : trimmedMarkerName;
    pendingScoreMarkers_.erase(std::remove_if(pendingScoreMarkers_.begin(),
                                              pendingScoreMarkers_.end(),
                                              [frame](const ScoreMarker& marker) { return marker.frame == frame; }),
                                pendingScoreMarkers_.end());
    pendingScoreMarkers_.push_back(ScoreMarker{
        .frame = frame,
        .label = effectiveMarkerName.toStdString(),
    });
    context_.selectFrame(frame);
    showPanel(QStringLiteral("score"));
    updateMovieViews();
    QMessageBox::information(this,
                             toQString(insertText.markerTitle),
                             toQString(insertMarkerPreparedText(effectiveMarkerName.toStdString(), frame)));
    if (statusLabel_) {
        statusLabel_->setText(toQString(insertMarkerStatusText(frame)));
    }
}

void MainWindow::removeCurrentFrame() {
    const auto insertText = insertActionText();
    const auto movie = context_.movie();
    if (!movie) {
        QMessageBox::information(this, toQString(insertText.removeFrameTitle), toQString(insertText.removeFrameNoMovieText));
        return;
    }

    const int frame = context_.currentFrame();
    const int count = movie->scoreChunk() ? std::max(1, movie->scoreChunk()->getFrameCount()) : 1;
    if (std::find(pendingRemovedFrames_.cbegin(), pendingRemovedFrames_.cend(), frame) == pendingRemovedFrames_.cend()) {
        pendingRemovedFrames_.push_back(frame);
    }
    context_.selectFrame(frame);
    showPanel(QStringLiteral("score"));
    updateMovieViews();
    QMessageBox::information(this,
                             toQString(insertText.removeFrameTitle),
                             toQString(removeFramePreparedText(frame, count)));
    if (statusLabel_) {
        statusLabel_->setText(toQString(removeFrameStatusText(frame)));
    }
}

void MainWindow::insertMediaElement(const QString& mediaType) {
    const auto insertText = insertActionText();
    if (!context_.movie()) {
        QMessageBox::information(this, toQString(insertText.mediaElementTitle), toQString(insertText.mediaElementNoMovieText));
        return;
    }

    const QString normalized = mediaType.trimmed();
    const auto* spec = findMediaElementSpec(normalized.toStdString());
    const QString targetPanel = spec != nullptr ? toQString(spec->targetPanelId) : QStringLiteral("cast");

    bool createdRuntimeMember = false;
    int createdCastLib = 0;
    int createdMemberNum = 0;
    QString createdDetails;
    const std::string memberType = spec != nullptr ? std::string(spec->id) : normalized.toStdString();
    if (stagePlayer_ && spec != nullptr && memberType != "film-loop") {
        const int selectedCastLib = castSelector_ ? castSelector_->currentData().toInt() : 0;
        const int runtimeCastLib = selectedCastLib > 0 ? selectedCastLib : 1;
        const auto created = stagePlayer_->castLibManager().createMember(runtimeCastLib, memberType);
        if (const auto* ref = created.asCastMemberRef()) {
            createdRuntimeMember = true;
            createdCastLib = ref->castLib == 1 ? 0 : ref->castLib;
            createdMemberNum = ref->memberNum();
            const std::string createdName = "New " + std::string(spec->menuText);
            (void)stagePlayer_->castLibManager().setMemberProp(ref->castLib,
                                                               createdMemberNum,
                                                               "name",
                                                               lingo::Datum::of(createdName));
            if (const auto member = stagePlayer_->castLibManager().resolveMember(ref->castLib, createdMemberNum)) {
                createdDetails = toQString(castMemberDetails(castMemberRowFromRuntimeMember(*member)));
            }
        }
    }

    if (createdRuntimeMember) {
        showPanel(QStringLiteral("cast"));
        updateMovieViews();
        context_.selectCastMember(createdCastLib, createdMemberNum);
        syncCastSelectionToTable(context_.selection());
        if (targetPanel != QStringLiteral("cast")) {
            loadCastMemberIntoPanel(targetPanel, createdDetails, createdCastLib, createdMemberNum);
            showPanel(targetPanel);
        }
    } else {
        showPanel(targetPanel);
    }
    QMessageBox::information(this,
                             toQString(insertText.mediaElementTitle),
                             toQString(insertMediaElementPreparedText(normalized.toStdString())));
    if (statusLabel_) {
        statusLabel_->setText(toQString(insertMediaElementStatusText(normalized.toStdString())));
    }
}

void MainWindow::editSelectedSpriteFrames() {
    const auto editText = editSpriteCommandText();
    const auto selection = context_.selection();
    if (selection.type != SelectionType::ScoreCell && selection.type != SelectionType::Sprite) {
        QMessageBox::information(this, toQString(editText.editFramesTitle), toQString(editText.editFramesNoSelectionText));
        return;
    }

    showPanel(QStringLiteral("score"));
    context_.selectSprite(selection.channel, selection.frame);
    syncScoreSelectionToViews(context_.selection());
    if (scoreTable_) {
        scoreTable_->setFocus(Qt::OtherFocusReason);
    } else if (scoreGrid_) {
        scoreGrid_->setFocus(Qt::OtherFocusReason);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(editSpriteFramesStatusText(selection.frame, scoreChannelName(selection.channel))));
    }
}

void MainWindow::editSelectedEntireSprite() {
    const auto editText = editSpriteCommandText();
    const auto movie = context_.movie();
    if (!movie) {
        QMessageBox::information(this, toQString(editText.editEntireTitle), toQString(editText.editEntireNoMovieText));
        return;
    }

    const auto selection = context_.selection();
    const auto sprite = findScoreIntervalForSelection(*movie, selection);
    if (!sprite || sprite->castMember <= 0) {
        QMessageBox::information(this,
                                 toQString(editText.editEntireTitle),
                                 toQString(editText.editEntireNoCastMemberText));
        return;
    }

    const int castLib = sprite->castLib;
    const int memberNum = sprite->castMember;
    const auto sourceFile = sourceFileForCastLib(castLib);
    const auto member = castMemberForCastLib(castLib, memberNum);
    if (!sourceFile || !member) {
        QMessageBox::information(this,
                                 toQString(editText.editEntireTitle),
                                 toQString(editText.editEntireMemberNotLoadedText));
        return;
    }

    const auto row = castMemberRowFromChunk(memberNum, *member);
    const QString targetPanel = toQString(editorPanelForMemberType(row.memberType));
    if (targetPanel.isEmpty()) {
        QMessageBox::information(this, toQString(editText.editEntireTitle), toQString(editText.editEntireNoPanelText));
        return;
    }

    const QString details = toQString(buildCastMemberEditorPreview(*sourceFile, row));
    context_.selectCastMember(castLib, memberNum);
    loadCastMemberIntoPanel(targetPanel, details, castLib, memberNum);
    if (statusLabel_) {
        statusLabel_->setText(toQString(editEntireSpriteStatusText(memberNum)));
    }
}

void MainWindow::showExchangeCastMembersDialog() {
    const auto refs = selectedCastMemberRefs();
    const auto exchangeText = exchangeCastMembersText();
    if (refs.size() != 2) {
        QMessageBox::information(this,
                                 toQString(exchangeText.title),
                                 toQString(exchangeText.selectTwoLoadedText));
        return;
    }

    struct ExchangeRow {
        CastMemberRef ref;
        CastMemberRow row;
        QString castName;
    };

    QList<ExchangeRow> rows;
    for (const auto& ref : refs) {
        const int runtimeCastLib = ref.castLib > 0 ? ref.castLib : 1;
        const auto member = stagePlayer_ ? stagePlayer_->castLibManager().resolveMember(runtimeCastLib, ref.memberNum)
                                         : nullptr;
        if (!member) {
            QMessageBox::information(this,
                                     toQString(exchangeText.title),
                                     toQString(exchangeText.oneMemberNotLoadedText));
            return;
        }
        QString castName = toQString(castWindowDefaultCastName());
        if (ref.castLib > 0 && stagePlayer_) {
            if (auto castLib = stagePlayer_->castLibManager().getCastLib(ref.castLib)) {
                castName = toQString(castLib->name());
                if (castName.trimmed().isEmpty()) {
                    castName = toQString(castWindowCastLibraryLabel(ref.castLib, {}, false, castLib->isExternal(), castLib->isLoaded()));
                }
            }
        }
        rows.push_back(ExchangeRow{
            .ref = ref,
            .row = castMemberRowFromRuntimeMember(*member),
            .castName = castName,
        });
    }

    showPanel(QStringLiteral("cast"));
    context_.selectCastMember(rows.front().ref.castLib, rows.front().ref.memberNum);
    syncCastSelectionToTable(context_.selection());

    QDialog dialog(this);
    dialog.setWindowTitle(toQString(exchangeText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* note = new QLabel(toQString(exchangeText.pendingNoteText), &dialog);
    note->setWordWrap(true);
    root->addWidget(note);

    auto* table = new QTableWidget(2, 7, &dialog);
    QStringList exchangeColumns;
    for (const auto column : exchangeCastMembersTableColumns()) {
        exchangeColumns.push_back(toQString(column));
    }
    table->setHorizontalHeaderLabels(exchangeColumns);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    for (int i = 0; i < rows.size(); ++i) {
        const auto& entry = rows[i];
        table->setItem(i, 0, new QTableWidgetItem(toQString(exchangeCastMembersSlotLabels()[i])));
        table->setItem(i, 1, new QTableWidgetItem(entry.castName));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(entry.ref.memberNum)));
        table->setItem(i,
                       3,
                       new QTableWidgetItem(entry.row.name.empty() ? toQString(exchangeText.unnamedMemberText)
                                                                   : toQString(entry.row.name)));
        table->setItem(i, 4, new QTableWidgetItem(toQString(entry.row.type)));
        table->setItem(i, 5, new QTableWidgetItem(toQString(exchangeCastMembersSizeText(entry.row.specificDataSize))));
        table->setItem(i,
                       6,
                       new QTableWidgetItem(entry.row.scriptId > 0 ? QString::number(entry.row.scriptId)
                                                                   : toQString(exchangeText.unsetText)));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(table);

    auto* details = new QPlainTextEdit(&dialog);
    details->setReadOnly(true);
    details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    details->setPlainText(toQString(exchangeCastMembersDetailsText(castMemberDetails(rows[0].row),
                                                                   castMemberDetails(rows[1].row))));
    root->addWidget(details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* exchangeButton = buttons->addButton(toQString(exchangeText.exchangeButtonText), QDialogButtonBox::AcceptRole);
    exchangeButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    dialog.resize(760, 460);
    if (dialog.exec() != QDialog::Accepted || !stagePlayer_) {
        return;
    }

    const auto first = rows[0].ref;
    const auto second = rows[1].ref;
    const int firstRuntimeCastLib = first.castLib > 0 ? first.castLib : 1;
    const int secondRuntimeCastLib = second.castLib > 0 ? second.castLib : 1;
    const bool exchanged = stagePlayer_->castLibManager().exchangeMemberMedia(firstRuntimeCastLib,
                                                                              first.memberNum,
                                                                              secondRuntimeCastLib,
                                                                              second.memberNum);
    if (exchanged) {
        castThumbnailCache_.clear();
        updateMovieViews();
        context_.selectCastMember(first.castLib, first.memberNum);
        syncCastSelectionToTable(context_.selection());
        refreshStageFrame();
        showPanel(QStringLiteral("cast"));
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(exchanged ? exchangeCastMembersAppliedStatusText(first.memberNum, second.memberNum)
                                                  : exchangeCastMembersFailedStatusText(first.memberNum, second.memberNum)));
    }
}

void MainWindow::performFocusedEditAction(const char* actionName, const QString& statusText) {
    auto* widget = QApplication::focusWidget();
    if (!widget || !QMetaObject::invokeMethod(widget, actionName, Qt::DirectConnection)) {
        if (statusLabel_) {
            statusLabel_->setText(toQString(editCommandStatusText().unsupportedText));
        }
        return;
    }
    if (statusLabel_) {
        statusLabel_->setText(statusText);
    }
}

void MainWindow::clearFocusedSelection() {
    auto* widget = QApplication::focusWidget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        if (lineEdit->hasSelectedText()) {
            lineEdit->del();
        } else {
            lineEdit->clear();
        }
    } else if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
        QTextCursor cursor = textEdit->textCursor();
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        } else {
            cursor.deleteChar();
        }
        textEdit->setTextCursor(cursor);
    } else if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
        QTextCursor cursor = plainTextEdit->textCursor();
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        } else {
            cursor.deleteChar();
        }
        plainTextEdit->setTextCursor(cursor);
    } else {
        if (statusLabel_) {
            statusLabel_->setText(toQString(editCommandStatusText().unsupportedClearText));
        }
        return;
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(editCommandStatusText().clearedText));
    }
}

void MainWindow::findInFocusedWidget() {
    const QString seed = focusedSelectedText().isEmpty() ? lastFindText_ : focusedSelectedText();
    const auto findText = findReplaceText();
    bool accepted = false;
    const QString term = QInputDialog::getText(this,
                                               toQString(findText.findTitle),
                                               toQString(findText.findPrompt),
                                               QLineEdit::Normal,
                                               seed,
                                               &accepted);
    if (!accepted || term.isEmpty()) {
        return;
    }
    lastFindText_ = term;
    (void)findTextInFocusedWidget(term);
}

void MainWindow::findAgainInFocusedWidget() {
    if (lastFindText_.isEmpty()) {
        findInFocusedWidget();
        return;
    }
    (void)findTextInFocusedWidget(lastFindText_);
}

void MainWindow::findSelectionInFocusedWidget() {
    const QString selection = focusedSelectedText();
    if (selection.isEmpty()) {
        if (statusLabel_) {
            statusLabel_->setText(toQString(findReplaceText().noSelectedTextToFind));
        }
        return;
    }
    lastFindText_ = selection;
    (void)findTextInFocusedWidget(selection);
}

void MainWindow::replaceInFocusedWidget() {
    const QString seed = focusedSelectedText().isEmpty() ? lastFindText_ : focusedSelectedText();
    const auto findText = findReplaceText();
    bool accepted = false;
    const QString term = QInputDialog::getText(this,
                                               toQString(findText.replaceTitle),
                                               toQString(findText.findPrompt),
                                               QLineEdit::Normal,
                                               seed,
                                               &accepted);
    if (!accepted || term.isEmpty()) {
        return;
    }

    const QString replacement = QInputDialog::getText(this,
                                                      toQString(findText.replaceTitle),
                                                      toQString(findText.replaceWithPrompt),
                                                      QLineEdit::Normal,
                                                      QString{},
                                                      &accepted);
    if (!accepted) {
        return;
    }

    lastFindText_ = term;
    const auto mode = QMessageBox::question(this,
                                            toQString(findText.replaceTitle),
                                            toQString(findReplaceAllPromptText(term.toStdString())),
                                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                            QMessageBox::No);
    if (mode == QMessageBox::Cancel) {
        return;
    }
    if (mode == QMessageBox::Yes) {
        const int count = replaceAllInFocusedWidget(term, replacement);
        if (statusLabel_) {
            statusLabel_->setText(toQString(count > 0 ? findReplaceAllStatusText(count, term.toStdString())
                                                       : findReplaceNotFoundStatusText(term.toStdString())));
        }
        return;
    }

    if (!findTextInFocusedWidget(term)) {
        return;
    }
    if (replaceSelectedTextInFocusedWidget(replacement) && statusLabel_) {
        statusLabel_->setText(toQString(findReplaceSingleStatusText(term.toStdString())));
    }
}

bool MainWindow::findTextInFocusedWidget(const QString& term) {
    if (term.isEmpty()) {
        return false;
    }
    auto* widget = QApplication::focusWidget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        const QString text = lineEdit->text();
        int found = text.indexOf(term, lineEdit->cursorPosition(), Qt::CaseInsensitive);
        if (found < 0) {
            found = text.indexOf(term, 0, Qt::CaseInsensitive);
        }
        if (found >= 0) {
            lineEdit->setSelection(found, term.size());
            if (statusLabel_) {
                statusLabel_->setText(toQString(findReplaceFoundStatusText(term.toStdString())));
            }
            return true;
        }
    } else if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
        if (!textEdit->find(term)) {
            QTextCursor cursor = textEdit->textCursor();
            cursor.movePosition(QTextCursor::Start);
            textEdit->setTextCursor(cursor);
            if (!textEdit->find(term)) {
                if (statusLabel_) {
                    statusLabel_->setText(toQString(findReplaceNotFoundStatusText(term.toStdString())));
                }
                return false;
            }
        }
        if (statusLabel_) {
            statusLabel_->setText(toQString(findReplaceFoundStatusText(term.toStdString())));
        }
        return true;
    } else if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
        if (!plainTextEdit->find(term)) {
            QTextCursor cursor = plainTextEdit->textCursor();
            cursor.movePosition(QTextCursor::Start);
            plainTextEdit->setTextCursor(cursor);
            if (!plainTextEdit->find(term)) {
                if (statusLabel_) {
                    statusLabel_->setText(toQString(findReplaceNotFoundStatusText(term.toStdString())));
                }
                return false;
            }
        }
        if (statusLabel_) {
            statusLabel_->setText(toQString(findReplaceFoundStatusText(term.toStdString())));
        }
        return true;
    }

    if (statusLabel_) {
        statusLabel_->setText(toQString(findReplaceText().noFocusedFindText));
    }
    return false;
}

bool MainWindow::replaceSelectedTextInFocusedWidget(const QString& replacement) {
    auto* widget = QApplication::focusWidget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        if (!lineEdit->hasSelectedText()) {
            if (statusLabel_) {
                statusLabel_->setText(toQString(findReplaceText().noSelectedTextToReplace));
            }
            return false;
        }
        lineEdit->insert(replacement);
        return true;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
        QTextCursor cursor = textEdit->textCursor();
        if (!cursor.hasSelection()) {
            if (statusLabel_) {
                statusLabel_->setText(toQString(findReplaceText().noSelectedTextToReplace));
            }
            return false;
        }
        cursor.insertText(replacement);
        textEdit->setTextCursor(cursor);
        return true;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
        QTextCursor cursor = plainTextEdit->textCursor();
        if (!cursor.hasSelection()) {
            if (statusLabel_) {
                statusLabel_->setText(toQString(findReplaceText().noSelectedTextToReplace));
            }
            return false;
        }
        cursor.insertText(replacement);
        plainTextEdit->setTextCursor(cursor);
        return true;
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(findReplaceText().noFocusedReplaceText));
    }
    return false;
}

int MainWindow::replaceAllInFocusedWidget(const QString& term, const QString& replacement) {
    if (term.isEmpty()) {
        return 0;
    }
    auto* widget = QApplication::focusWidget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        QString text = lineEdit->text();
        int count = 0;
        int position = 0;
        while ((position = text.indexOf(term, position, Qt::CaseInsensitive)) >= 0) {
            text.replace(position, term.size(), replacement);
            position += replacement.size();
            ++count;
        }
        if (count > 0) {
            lineEdit->setText(text);
        }
        return count;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
        int count = 0;
        QTextCursor cursor(textEdit->document());
        cursor.beginEditBlock();
        QTextCursor match = textEdit->document()->find(term, cursor);
        while (!match.isNull()) {
            match.insertText(replacement);
            cursor = match;
            ++count;
            match = textEdit->document()->find(term, cursor);
            if (match.isNull()) {
                break;
            }
        }
        cursor.endEditBlock();
        return count;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
        int count = 0;
        QTextCursor cursor(plainTextEdit->document());
        cursor.beginEditBlock();
        QTextCursor match = plainTextEdit->document()->find(term, cursor);
        while (!match.isNull()) {
            match.insertText(replacement);
            cursor = match;
            ++count;
            match = plainTextEdit->document()->find(term, cursor);
            if (match.isNull()) {
                break;
            }
        }
        cursor.endEditBlock();
        return count;
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(findReplaceText().noFocusedReplaceText));
    }
    return 0;
}

QString MainWindow::focusedSelectedText() const {
    auto* widget = QApplication::focusWidget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        return lineEdit->selectedText();
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
        return textEdit->textCursor().selectedText();
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
        return plainTextEdit->textCursor().selectedText();
    }
    return {};
}

void MainWindow::resetLayout() {
    QSettings settings;
    settings.remove(QStringLiteral("editor/mainWindowGeometry"));
    settings.remove(QStringLiteral("editor/mainWindowState"));
    applyDefaultLayout();
    saveLayout();
    if (statusLabel_) {
        statusLabel_->setText(toQString(mainWindowShellText().layoutResetStatusText));
    }
}

void MainWindow::dockPanelToArea(QDockWidget* dock, DockArea area) {
    if (!dock) {
        return;
    }
    dock->setFloating(false);
    if (area == DockArea::Center) {
        auto* stageDock = panels_.value(QStringLiteral("stage"), nullptr);
        if (stageDock && stageDock != dock) {
            tabifyDockWidget(stageDock, dock);
        } else {
            addDockWidget(Qt::LeftDockWidgetArea, dock);
        }
    } else {
        addDockWidget(qtDockArea(area), dock);
    }
    dock->show();
    dock->raise();
    saveLayout();
}

void MainWindow::splitPanelFromTabGroup(QDockWidget* dock, DockArea direction) {
    if (!dock) {
        return;
    }
    const auto siblings = tabifiedDockWidgets(dock);
    if (siblings.isEmpty()) {
        return;
    }

    auto* reference = siblings.front();
    const auto area = dockWidgetArea(reference);
    dock->setFloating(false);
    addDockWidget(area == Qt::NoDockWidgetArea ? Qt::LeftDockWidgetArea : area, dock);

    if (direction == DockArea::Left) {
        splitDockWidget(dock, reference, Qt::Horizontal);
    } else if (direction == DockArea::Right) {
        splitDockWidget(reference, dock, Qt::Horizontal);
    } else if (direction == DockArea::Top) {
        splitDockWidget(dock, reference, Qt::Vertical);
    } else {
        splitDockWidget(reference, dock, Qt::Vertical);
    }

    dock->show();
    dock->raise();
    saveLayout();
}

void MainWindow::saveLayout() {
    QSettings settings;
    settings.setValue(QStringLiteral("editor/mainWindowGeometry"), saveGeometry());
    settings.setValue(QStringLiteral("editor/mainWindowState"), saveState(1));
}

bool MainWindow::restoreSavedLayout() {
    QSettings settings;
    const auto geometry = settings.value(QStringLiteral("editor/mainWindowGeometry")).toByteArray();
    const auto state = settings.value(QStringLiteral("editor/mainWindowState")).toByteArray();
    if (geometry.isEmpty() || state.isEmpty()) {
        return false;
    }
    restoreGeometry(geometry);
    return restoreState(state, 1);
}

void MainWindow::applyDefaultLayout() {
    for (const auto& spec : editorPanelSpecs()) {
        const QString id = toQString(spec.id);
        if (auto found = panels_.find(id); found != panels_.end()) {
            addDockWidget(qtDockArea(spec.defaultDockArea), found.value());
            found.value()->setFloating(false);
            found.value()->setVisible(spec.visibleByDefault);
        }
    }
    tabifyDockWidget(panels_.value(QStringLiteral("score")), panels_.value(QStringLiteral("script")));
    tabifyDockWidget(panels_.value(QStringLiteral("script")), panels_.value(QStringLiteral("message")));
    tabifyDockWidget(panels_.value(QStringLiteral("cast")), panels_.value(QStringLiteral("property-inspector")));
    panels_.value(QStringLiteral("stage"))->raise();
    panels_.value(QStringLiteral("score"))->raise();
    panels_.value(QStringLiteral("cast"))->raise();
}

void MainWindow::updateMovieViews() {
    const auto movie = context_.movie();
    const QString fileName = context_.currentPath().empty()
        ? toQString(mainWindowSummaryText().noMovieOpenText)
        : QString::fromStdString(context_.currentPath().filename().string());
    const auto summary = movie ? buildMovieSummary(*movie) : MovieSummary{};

    if (stageSummary_) {
        stageSummary_->setText(movie ? toQString(stageWindowSummaryText(fileName.toStdString(),
                                                                        summary.stageWidth,
                                                                        summary.stageHeight,
                                                                        context_.currentFrame()))
                                     : toQString(stageWindowNoMovieText()));
    }
    if (stageCanvasFrame_) {
        updateStageCanvasSize();
    }
    if (movieSummary_) {
        movieSummary_->setText(movie ? toQString(mainWindowMovieSummaryText(summary))
                                     : toQString(mainWindowSummaryText().moviePlaceholderText));
    }
    if (!propertyMovieFields_.isEmpty()) {
        if (movie) {
            const auto values = propertyInspectorMovieValues(summary, fileName.toStdString());
            setPropertyGridValues(propertyMovieFields_, values);
        } else {
            clearPropertyGridValues(propertyMovieFields_);
        }
    }
    updatePropertyInspectorForSelection(context_.selection());
    if (castSummary_) {
        const auto castText = castWindowText();
        castSummary_->setText(movie ? toQString(mainWindowCastSummaryText(summary))
                                    : toQString(castText.emptySummaryText));
    }
    if (castSelector_) {
        const bool blocked = castSelector_->blockSignals(true);
        const int previousCastLib = castSelector_->currentData().toInt();
        castSelector_->clear();
        const auto entries = runtimeCastSelectorEntries(stagePlayer_.get());
        for (const auto& entry : entries) {
            castSelector_->addItem(entry.label, entry.castLibNumber);
        }
        const int restoredIndex = castSelector_->findData(previousCastLib);
        if (restoredIndex >= 0) {
            castSelector_->setCurrentIndex(restoredIndex);
        }
        castSelector_->blockSignals(blocked);
    }
    if (scoreSummary_) {
        scoreSummary_->setText(movie ? toQString(mainWindowScoreSummaryText(summary))
                                     : toQString(mainWindowSummaryText().scorePlaceholderText));
    }
    if (scoreStatusLabel_) {
        scoreStatusLabel_->setText(movie ? toQString(scoreLoadedStatusText(summary.frameCount, summary.channelCount))
                                         : toQString(scoreInitialStatusText()));
    }
    std::vector<ScoreMarker> scoreMarkers;
    if (movie) {
        if (const auto frameLabels = movie->frameLabelsChunk()) {
            scoreMarkers.reserve(frameLabels->labels().size());
            for (const auto& label : frameLabels->labels()) {
                scoreMarkers.push_back(ScoreMarker{
                    .frame = label.frameNum.value(),
                    .label = label.label,
                });
            }
        }
    }
    for (const auto& marker : pendingScoreMarkers_) {
        if (marker.frame >= 1 && marker.frame <= summary.frameCount) {
            scoreMarkers.erase(std::remove_if(scoreMarkers.begin(),
                                              scoreMarkers.end(),
                                              [&marker](const ScoreMarker& existing) {
                                                  return existing.frame == marker.frame;
                                              }),
                                scoreMarkers.end());
            scoreMarkers.push_back(marker);
        }
    }
    std::sort(scoreMarkers.begin(), scoreMarkers.end(), [](const ScoreMarker& left, const ScoreMarker& right) {
        if (left.frame != right.frame) {
            return left.frame < right.frame;
        }
        return left.label < right.label;
    });
    if (scoreMarkers_) {
        scoreMarkers_->setMarkers(scoreMarkers, movie ? summary.frameCount : 1);
        scoreMarkers_->setCurrentFrame(context_.currentFrame());
        scoreMarkers_->setToolTip(toQString(scoreMarkersText(scoreMarkers)));
    }
    if (scoreChannelHeader_) {
        const int channelCount = movie ? summary.channelCount : static_cast<int>(scoreSpecialChannelNames().size()) + 48;
        scoreChannelHeader_->setChannelCount(channelCount);
    }
    if (scriptSummary_) {
        scriptSummary_->setText(movie ? toQString(scriptEditorSummaryText(summary.scriptCount,
                                                                          summary.globalCount,
                                                                          summary.propertyCount))
                                      : toQString(scriptEditorText().emptySummaryText));
    }

    if (castTable_) {
        const bool hadSelectionSignals = castTable_->blockSignals(true);
        const bool hadGridSelectionSignals = castGrid_ ? castGrid_->blockSignals(true) : false;
        castTable_->setSortingEnabled(false);
        castTable_->setRowCount(0);
        if (castGrid_) {
            castGrid_->clear();
        }
        int totalMembers = 0;
        if (movie) {
            const std::string filter = castSearch_ ? castSearch_->text().trimmed().toStdString() : std::string();
            const std::string typeFilter =
                castTypeFilter_ ? castTypeFilter_->currentText().toStdString() : std::string();
            const int selectedCastLib = castSelector_ ? castSelector_->currentData().toInt() : 0;
            std::shared_ptr<DirectorFile> castSourceFile = movie;
            std::vector<CastMemberRow> rows;
            if (selectedCastLib > 0 && stagePlayer_) {
                if (auto castLib = stagePlayer_->castLibManager().getCastLib(selectedCastLib);
                    castLib && castLib->isLoaded()) {
                    rows = buildRuntimeCastMemberRows(*castLib, filter);
                    if (auto sourceFile = castLib->sourceFile()) {
                        castSourceFile = std::move(sourceFile);
                    }
                }
            } else {
                rows = buildCastMemberRows(*movie, filter);
            }
            totalMembers = static_cast<int>(rows.size());
            for (const auto& member : rows) {
                if (!castMemberMatchesTypeFilter(member, typeFilter)) {
                    continue;
                }
                const QString name = toQString(member.name);
                const int row = castTable_->rowCount();
                castTable_->insertRow(row);
                auto* previewItem = item(QString());
                QPixmap thumbnail;
                const int thumbnailKey = (selectedCastLib << 16) ^ member.chunkId;
                const auto cachedThumbnail = castThumbnailCache_.constFind(thumbnailKey);
                if (cachedThumbnail != castThumbnailCache_.cend()) {
                    thumbnail = cachedThumbnail.value();
                } else {
                    thumbnail = placeholderThumbnail(toQString(castThumbnailTypeAbbreviation(member.memberType)));
                    if (member.memberType == cast::MemberType::Bitmap || member.memberType == cast::MemberType::Picture) {
                        if (const auto castMember = castMemberForCastLib(selectedCastLib, member.chunkId)) {
                            if (const auto decoded = castSourceFile->decodeBitmap(castMember)) {
                                thumbnail = bitmapThumbnailPixmap(*decoded);
                            }
                        }
                    }
                    castThumbnailCache_.insert(thumbnailKey, thumbnail);
                }
                previewItem->setIcon(QIcon(thumbnail));
                castTable_->setItem(row, 0, previewItem);
                castTable_->setItem(row, 1, item(member.chunkId));
                castTable_->setItem(row, 2, item(toQString(member.type)));
                const QString displayName = toQString(castWindowMemberDisplayName(name.toStdString()));
                castTable_->setItem(row, 3, item(displayName));
                castTable_->setItem(row, 4, item(member.scriptId));
                castTable_->setItem(row, 5, item(toQString(registrationPointText(member.regPointX, member.regPointY))));
                const QString details = castSourceFile ? toQString(buildCastMemberEditorPreview(*castSourceFile, member))
                                                       : toQString(castMemberDetails(member));
                setRowData(castTable_,
                           row,
                           member,
                           details,
                           selectedCastLib);
                if (castGrid_) {
                    auto* gridItem = new QListWidgetItem(QIcon(thumbnail), displayName, castGrid_);
                    gridItem->setTextAlignment(Qt::AlignCenter);
                    gridItem->setSizeHint(QSize(castThumbnailSize() + 36, castThumbnailSize() + 34));
                    gridItem->setToolTip(toQString(castMemberDetails(member)));
                    const QString targetPanel = toQString(editorPanelForMemberType(member.memberType));
                    gridItem->setData(kCastDetailsRole, details);
                    gridItem->setData(kCastTargetPanelRole, targetPanel);
                    gridItem->setData(kCastMemberNumberRole, member.chunkId);
                    gridItem->setData(kCastLibraryNumberRole, selectedCastLib);
                    gridItem->setData(kCastDisplayNameRole, displayName);
                }
            }
        }
        const bool gridMode = castGridViewButton_ && castGridViewButton_->isChecked();
        const auto viewSpec = gridMode ? castWindowGridViewModeSpec() : castWindowListViewModeSpec();
        applyCastWindowViewMode(castTable_, viewSpec);
        castTable_->setVisible(!gridMode);
        if (castGrid_) {
            castGrid_->setVisible(gridMode);
        }
        castTable_->resizeColumnsToContents();
        castTable_->setColumnWidth(0, viewSpec.previewColumnWidth);
        castTable_->setSortingEnabled(true);
        castTable_->blockSignals(hadSelectionSignals);
        if (castGrid_) {
            castGrid_->blockSignals(hadGridSelectionSignals);
        }
        if (memberSummary_) {
            memberSummary_->setPlainText(toQString(castWindowText().selectedMemberPlaceholderText));
        }
        if (castStatusLabel_) {
            if (!movie) {
                castStatusLabel_->setText(toQString(castWindowReadyStatus()));
            } else if (totalMembers == 0) {
                castStatusLabel_->setText(toQString(castWindowNoMembersStatusText()));
            } else {
                castStatusLabel_->setText(toQString(castWindowMemberCountStatus(castTable_->rowCount(), totalMembers)));
            }
        }
        if (openCastMemberButton_) {
            openCastMemberButton_->setEnabled(false);
        }
    }

    std::vector<ScoreIntervalRow> scoreRows;
    if (movie && (scoreTable_ || scoreGrid_)) {
        scoreRows = buildScoreIntervalRows(*movie);
    }
    if (scoreGrid_) {
        const int channelCount = movie ? summary.channelCount : static_cast<int>(scoreSpecialChannelNames().size()) + 48;
        const int frameCount = movie ? summary.frameCount : 1;
        std::vector<PendingScoreKeyframe> visibleKeyframes;
        std::vector<int> visibleRemovedFrames;
        if (movie) {
            for (const auto& keyframe : pendingScoreKeyframes_) {
                if (keyframe.channel >= 0 && keyframe.channel < channelCount &&
                    keyframe.frame >= 1 && keyframe.frame <= frameCount) {
                    visibleKeyframes.push_back(keyframe);
                }
            }
            for (const int frame : pendingRemovedFrames_) {
                if (frame >= 1 && frame <= frameCount) {
                    visibleRemovedFrames.push_back(frame);
                }
            }
        }
        scoreGrid_->setScoreData(scoreRows, frameCount, channelCount);
        scoreGrid_->setPendingKeyframes(std::move(visibleKeyframes));
        scoreGrid_->setPendingRemovedFrames(std::move(visibleRemovedFrames));
        scoreGrid_->setCurrentFrame(context_.currentFrame());
    }
    if (scoreTable_) {
        const bool hadSelectionSignals = scoreTable_->blockSignals(true);
        scoreTable_->setSortingEnabled(false);
        scoreTable_->setRowCount(0);
        if (movie) {
            for (const auto& interval : scoreRows) {
                const int row = scoreTable_->rowCount();
                scoreTable_->insertRow(row);
                scoreTable_->setItem(row, 0, item(interval.startFrame));
                scoreTable_->setItem(row, 1, item(interval.endFrame));
                scoreTable_->setItem(row, 2, item(toQString(scoreIntervalChannelDisplayText(interval.channel))));
                scoreTable_->setItem(row, 3, item(interval.castLib));
                scoreTable_->setItem(row, 4, item(interval.castMember));
                scoreTable_->setItem(row, 5, item(toQString(interval.memberTypeName)));
                scoreTable_->setItem(row, 6, item(toQString(interval.memberName)));
                setRowData(scoreTable_, row, interval);
            }
        }
        scoreTable_->resizeColumnsToContents();
        scoreTable_->setSortingEnabled(true);
        scoreTable_->blockSignals(hadSelectionSignals);
        updateScorePlaybackHead(context_.currentFrame());
        if (scoreStatusLabel_ && movie && scoreTable_->rowCount() == 0) {
            scoreStatusLabel_->setText(toQString(scoreNoDataStatusText()));
        }
    }

    if (scriptTable_) {
        scriptTable_->setSortingEnabled(false);
        scriptTable_->setRowCount(0);
        if (scriptPreview_) {
            scriptPreview_->setPlainText(toQString(scriptEditorInitialText()));
        }
        if (scriptCastSelector_) {
            const bool blocked = scriptCastSelector_->blockSignals(true);
            const int previousCastLib = scriptCastSelector_->currentData().toInt();
            scriptCastSelector_->clear();
            const auto entries = runtimeCastSelectorEntries(stagePlayer_.get());
            for (const auto& entry : entries) {
                scriptCastSelector_->addItem(entry.label, entry.castLibNumber);
            }
            const int restoredIndex = scriptCastSelector_->findData(previousCastLib);
            if (restoredIndex >= 0) {
                scriptCastSelector_->setCurrentIndex(restoredIndex);
            }
            scriptCastSelector_->blockSignals(blocked);
        }
        if (scriptSelector_) {
            const bool blocked = scriptSelector_->blockSignals(true);
            scriptSelector_->clear();
            scriptSelector_->blockSignals(blocked);
        }
        if (scriptHandlerSelector_) {
            const bool blocked = scriptHandlerSelector_->blockSignals(true);
            scriptHandlerSelector_->clear();
            scriptHandlerSelector_->blockSignals(blocked);
        }
        if (movie) {
            std::shared_ptr<DirectorFile> scriptSourceFile = movie;
            const int selectedScriptCastLib = scriptCastSelector_ ? scriptCastSelector_->currentData().toInt() : 0;
            if (selectedScriptCastLib > 0 && stagePlayer_) {
                if (auto castLib = stagePlayer_->castLibManager().getCastLib(selectedScriptCastLib);
                    castLib && castLib->isLoaded()) {
                    if (auto sourceFile = castLib->sourceFile()) {
                        scriptSourceFile = std::move(sourceFile);
                    }
                }
            }
            for (const auto& script : buildScriptRows(*scriptSourceFile)) {
                const int row = scriptTable_->rowCount();
                scriptTable_->insertRow(row);
                scriptTable_->setItem(row, 0, item(script.scriptId));
                scriptTable_->setItem(row, 1, item(toQString(script.name)));
                scriptTable_->setItem(row, 2, item(toQString(script.type)));
                scriptTable_->setItem(row, 3, item(script.handlerCount));
                scriptTable_->setItem(row, 4, item(script.globalCount));
                scriptTable_->setItem(row, 5, item(script.propertyCount));

                QStringList handlerPreviews;
                for (const auto& handlerPreview : script.handlers) {
                    handlerPreviews.push_back(toQString(handlerPreview));
                }
                QStringList lingoPreviews;
                for (const auto& lingoPreview : script.lingoHandlers) {
                    lingoPreviews.push_back(toQString(lingoPreview));
                }
                if (scriptSelector_) {
                    scriptSelector_->addItem(toQString(scriptEditorSelectorLabel(script.name, script.scriptId, script.type)),
                                             handlerPreviews);
                    scriptSelector_->setItemData(scriptSelector_->count() - 1, script.scriptId, Qt::UserRole + 1);
                    scriptSelector_->setItemData(scriptSelector_->count() - 1, lingoPreviews, Qt::UserRole + 3);
                }
                for (int column = 0; column < scriptTable_->columnCount(); ++column) {
                    if (auto* tableItem = scriptTable_->item(row, column)) {
                        tableItem->setData(Qt::UserRole, handlerPreviews);
                        tableItem->setData(Qt::UserRole + 1, script.scriptId);
                        tableItem->setData(Qt::UserRole + 3, lingoPreviews);
                    }
                }
            }
            if (scriptSelector_ && scriptSelector_->count() == 0) {
                scriptSelector_->addItem(toQString(scriptEditorNoScriptsText()));
            }
            if (scriptTable_->rowCount() > 0) {
                scriptTable_->selectRow(0);
            }
        } else if (scriptSelector_) {
            scriptSelector_->addItem(toQString(scriptEditorNoScriptsText()));
        }
        scriptTable_->resizeColumnsToContents();
        scriptTable_->setSortingEnabled(true);
        updateScriptEditorPreview();
    }

    if (debugScriptSelector_) {
        debugScriptLabels_.clear();
        debugScriptIds_.clear();
        debugScriptHandlerPreviews_.clear();
        debugScriptLingoHandlerPreviews_.clear();
        debugScriptLiterals_.clear();
        debugScriptProperties_.clear();
        debugScriptGlobals_.clear();
        if (movie) {
            std::vector<std::shared_ptr<DirectorFile>> scriptSources{movie};
            if (stagePlayer_) {
                for (const auto& [castLibNumber, castLib] : stagePlayer_->castLibManager().castLibs()) {
                    if (castLibNumber == 1 || !castLib || !castLib->isExternal() || !castLib->isLoaded()) {
                        continue;
                    }
                    if (auto sourceFile = castLib->sourceFile()) {
                        scriptSources.push_back(std::move(sourceFile));
                    }
                }
            }
            for (const auto& scriptSource : scriptSources) {
                if (!scriptSource) {
                    continue;
                }
                for (const auto& script : buildScriptRows(*scriptSource)) {
                QStringList handlerPreviews;
                for (const auto& handlerPreview : script.handlers) {
                    handlerPreviews.push_back(toQString(handlerPreview));
                }
                QStringList lingoPreviews;
                for (const auto& lingoPreview : script.lingoHandlers) {
                    lingoPreviews.push_back(toQString(lingoPreview));
                }
                debugScriptLabels_.push_back(toQString(scriptEditorSelectorLabel(script.name, script.scriptId, script.type)));
                debugScriptIds_.push_back(script.scriptId);
                debugScriptHandlerPreviews_.push_back(handlerPreviews);
                debugScriptLingoHandlerPreviews_.push_back(lingoPreviews);
                QStringList literals;
                for (const auto& literal : script.literals) {
                    literals.push_back(toQString(literal));
                }
                QStringList properties;
                for (const auto& property : script.properties) {
                    properties.push_back(toQString(property));
                }
                QStringList globals;
                for (const auto& global : script.globals) {
                    globals.push_back(toQString(global));
                }
                debugScriptLiterals_.push_back(literals);
                debugScriptProperties_.push_back(properties);
                debugScriptGlobals_.push_back(globals);
                }
            }
        }
        refreshBytecodeDebuggerScriptFilter();
    }
    syncSelectionToPanels(context_.selection());
    updateFrameLabel(context_.currentFrame());
}

void MainWindow::syncSelectionToPanels(const SelectionState& selection) {
    syncCastSelectionToTable(selection);
    syncScoreSelectionToViews(selection);
}

void MainWindow::syncCastSelectionToTable(const SelectionState& selection) {
    if (!castTable_ && !castGrid_) {
        return;
    }
    const bool blocked = castTable_ ? castTable_->blockSignals(true) : false;
    const bool gridBlocked = castGrid_ ? castGrid_->blockSignals(true) : false;
    bool found = false;
    if (selection.type == SelectionType::CastMember && castTable_) {
        for (int row = 0; row < castTable_->rowCount(); ++row) {
            const auto* rowItem = castTable_->item(row, 0);
            if (!rowItem || rowItem->data(kCastMemberNumberRole).toInt() != selection.memberNum ||
                rowItem->data(kCastLibraryNumberRole).toInt() != selection.castLib) {
                continue;
            }
            castTable_->selectRow(row);
            castTable_->scrollToItem(rowItem, QAbstractItemView::PositionAtCenter);
            if (memberSummary_) {
                const QString details = rowItem->data(kCastDetailsRole).toString();
                memberSummary_->setPlainText(details.isEmpty() ? toQString(castWindowText().selectedMemberPlaceholderText) : details);
            }
            if (openCastMemberButton_) {
                openCastMemberButton_->setEnabled(!rowItem->data(kCastTargetPanelRole).toString().isEmpty());
            }
            found = true;
            break;
        }
    }
    if (selection.type == SelectionType::CastMember && castGrid_) {
        for (int row = 0; row < castGrid_->count(); ++row) {
            auto* item = castGrid_->item(row);
            if (!item || item->data(kCastMemberNumberRole).toInt() != selection.memberNum ||
                item->data(kCastLibraryNumberRole).toInt() != selection.castLib) {
                continue;
            }
            item->setSelected(true);
            castGrid_->setCurrentItem(item);
            castGrid_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            if (!found && memberSummary_) {
                const QString details = item->data(kCastDetailsRole).toString();
                memberSummary_->setPlainText(details.isEmpty() ? toQString(castWindowText().selectedMemberPlaceholderText) : details);
            }
            if (!found && openCastMemberButton_) {
                openCastMemberButton_->setEnabled(!item->data(kCastTargetPanelRole).toString().isEmpty());
            }
            found = true;
            break;
        }
    }
    if (!found) {
        if (castTable_) {
            castTable_->clearSelection();
        }
        if (castGrid_) {
            castGrid_->clearSelection();
        }
        if (memberSummary_) {
            memberSummary_->setPlainText(toQString(castWindowText().selectedMemberPlaceholderText));
        }
        if (openCastMemberButton_) {
            openCastMemberButton_->setEnabled(false);
        }
    }
    if (castTable_) {
        castTable_->blockSignals(blocked);
    }
    if (castGrid_) {
        castGrid_->blockSignals(gridBlocked);
    }
}

void MainWindow::syncScoreSelectionToViews(const SelectionState& selection) {
    const bool hasScoreSelection = selection.type == SelectionType::ScoreCell || selection.type == SelectionType::Sprite;
    if (scoreGrid_) {
        if (hasScoreSelection) {
            scoreGrid_->setSelectedCell(selection.channel, selection.frame);
        } else {
            scoreGrid_->clearSelectedCell();
        }
    }
    if (scoreChannelHeader_) {
        if (hasScoreSelection) {
            scoreChannelHeader_->setSelectedChannel(selection.channel);
        } else {
            scoreChannelHeader_->clearSelectedChannel();
        }
    }
    if (!scoreTable_) {
        if (scoreStatusLabel_) {
            scoreStatusLabel_->setText(hasScoreSelection ? toQString(scoreCellStatusText(selection.frame, selection.channel))
                                                         : toQString(scoreFrameStatusText(context_.currentFrame())));
        }
        return;
    }
    const bool blocked = scoreTable_->blockSignals(true);
    bool found = false;
    if (hasScoreSelection) {
        for (int row = 0; row < scoreTable_->rowCount(); ++row) {
            const auto* rowItem = scoreTable_->item(row, 0);
            if (!rowItem) {
                continue;
            }
            const int startFrame = rowItem->data(Qt::UserRole).toInt();
            const int channel = rowItem->data(Qt::UserRole + 1).toInt();
            const int endFrame = rowItem->data(Qt::UserRole + 2).toInt();
            if (channel != selection.channel || selection.frame < startFrame || selection.frame > endFrame) {
                continue;
            }
            scoreTable_->selectRow(row);
            scoreTable_->scrollToItem(rowItem, QAbstractItemView::PositionAtCenter);
            found = true;
            break;
        }
    }
    if (!found) {
        scoreTable_->clearSelection();
    }
    scoreTable_->blockSignals(blocked);
    if (scoreStatusLabel_) {
        scoreStatusLabel_->setText(hasScoreSelection ? toQString(scoreCellStatusText(selection.frame, selection.channel))
                                                     : toQString(scoreFrameStatusText(context_.currentFrame())));
    }
}

void MainWindow::updateFrameLabel(int frame) {
    const auto movie = context_.movie();
    const int count = movie && movie->scoreChunk() ? movie->scoreChunk()->getFrameCount() : 1;
    frameLabel_->setText(toQString(playbackFrameText(frame, count)));
    if (stageSummary_ && movie) {
        stageSummary_->setText(toQString(stageWindowSummaryText(context_.currentPath().filename().string(),
                                                                movie->stageWidth(),
                                                                movie->stageHeight(),
                                                                frame)));
    }
    refreshStageFrame();
    if (scoreStatusLabel_) {
        scoreStatusLabel_->setText(toQString(scoreFrameStatusText(frame)));
    }
    updateScorePlaybackHead(frame);
}

void MainWindow::updateScorePlaybackHead(int frame) {
    if (scoreGrid_) {
        scoreGrid_->setCurrentFrame(frame);
    }
    if (scoreMarkers_) {
        scoreMarkers_->setCurrentFrame(frame);
    }
    if (!scoreTable_) {
        return;
    }
    for (int row = 0; row < scoreTable_->rowCount(); ++row) {
        const auto* firstItem = scoreTable_->item(row, 0);
        if (!firstItem) {
            continue;
        }
        const ScoreIntervalRow interval{
            .startFrame = firstItem->data(Qt::UserRole).toInt(),
            .endFrame = firstItem->data(Qt::UserRole + 2).toInt(),
            .castMember = scoreTable_->item(row, 4) ? scoreTable_->item(row, 4)->text().toInt() : -1,
        };
        const bool active = scoreIntervalContainsFrame(interval, frame);
        const auto background = active ? colorFromRgb(scoreActiveFrameBackgroundRgb())
                                       : firstItem->data(Qt::UserRole + 3).value<QColor>();
        QFont font = firstItem->font();
        font.setBold(active);
        for (int column = 0; column < scoreTable_->columnCount(); ++column) {
            if (auto* tableItem = scoreTable_->item(row, column)) {
                if (!active) {
                    tableItem->setBackground(tableItem->data(Qt::UserRole + 3).value<QColor>());
                } else {
                    tableItem->setBackground(background);
                }
                tableItem->setFont(font);
            }
        }
    }
}

void MainWindow::setStageZoom(int percent) {
    stageZoomPercent_ = std::clamp(percent, 25, 400);
    EditorPreferences::setStageZoomPercent(stageZoomPercent_);
    for (auto* action : stageZoomActions_) {
        if (action) {
            action->setChecked(action->data().toInt() == stageZoomPercent_);
        }
    }
    updateStageCanvasSize();
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewStageZoomStatusText(stageZoomPercent_)));
    }
}

void MainWindow::updateStageCanvasSize() {
    if (!stageCanvasFrame_) {
        return;
    }
    const auto movie = context_.movie();
    const int logicalWidth = movie && movie->stageWidth() > 0 ? movie->stageWidth() : stageWindowDefaultWidth();
    const int logicalHeight = movie && movie->stageHeight() > 0 ? movie->stageHeight() : stageWindowDefaultHeight();
    const double scale = static_cast<double>(stageZoomPercent_) / 100.0;
    const QSize scaledSize(std::max(1, static_cast<int>(std::lround(logicalWidth * scale))),
                           std::max(1, static_cast<int>(std::lround(logicalHeight * scale))));
    stageCanvasFrame_->setFixedSize(scaledSize);
}

void MainWindow::setScoreKeyframesVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("scoreKeyframes"), visible);
    if (scoreKeyframesAction_ && scoreKeyframesAction_->isChecked() != visible) {
        const bool blocked = scoreKeyframesAction_->blockSignals(true);
        scoreKeyframesAction_->setChecked(visible);
        scoreKeyframesAction_->blockSignals(blocked);
    }
    if (scoreMarkerRow_) {
        scoreMarkerRow_->setVisible(visible);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("keyframes", visible)));
    }
}

void MainWindow::setScoreGridLinesVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("scoreGridLines"), visible);
    if (scoreGridLinesAction_ && scoreGridLinesAction_->isChecked() != visible) {
        const bool blocked = scoreGridLinesAction_->blockSignals(true);
        scoreGridLinesAction_->setChecked(visible);
        scoreGridLinesAction_->blockSignals(blocked);
    }
    if (scoreGrid_) {
        scoreGrid_->setGridLinesVisible(visible);
    }
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("score-grid-lines", visible)));
    }
}

void MainWindow::setSpriteOverlayInfoVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("spriteOverlayInfo"), visible);
    EditorPreferences::setPreferenceBool(QStringLiteral("sprite"), QStringLiteral("showOverlays"), visible);
    if (spriteOverlayInfoAction_ && spriteOverlayInfoAction_->isChecked() != visible) {
        const bool blocked = spriteOverlayInfoAction_->blockSignals(true);
        spriteOverlayInfoAction_->setChecked(visible);
        spriteOverlayInfoAction_->blockSignals(blocked);
    }
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("sprite-overlay-info", visible)));
    }
}

void MainWindow::setSpriteOverlayPathsVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("spriteOverlayPaths"), visible);
    EditorPreferences::setPreferenceBool(QStringLiteral("sprite"), QStringLiteral("showPaths"), visible);
    if (spriteOverlayPathsAction_ && spriteOverlayPathsAction_->isChecked() != visible) {
        const bool blocked = spriteOverlayPathsAction_->blockSignals(true);
        spriteOverlayPathsAction_->setChecked(visible);
        spriteOverlayPathsAction_->blockSignals(blocked);
    }
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("sprite-overlay-paths", visible)));
    }
}

void MainWindow::setSpriteToolbarVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("spriteToolbar"), visible);
    if (spriteToolbarAction_ && spriteToolbarAction_->isChecked() != visible) {
        const bool blocked = spriteToolbarAction_->blockSignals(true);
        spriteToolbarAction_->setChecked(visible);
        spriteToolbarAction_->blockSignals(blocked);
    }
    if (auto* dock = panels_.value(QStringLiteral("tool-palette"), nullptr)) {
        dock->setVisible(visible);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("sprite-toolbar", visible)));
    }
}

void MainWindow::setStageGridSnapEnabled(bool enabled) {
    EditorPreferences::setViewOption(QStringLiteral("stageGridSnap"), enabled);
    EditorPreferences::setPreferenceBool(QStringLiteral("sprite"), QStringLiteral("snapToGrid"), enabled);
    if (stageGridSnapAction_ && stageGridSnapAction_->isChecked() != enabled) {
        const bool blocked = stageGridSnapAction_->blockSignals(true);
        stageGridSnapAction_->setChecked(enabled);
        stageGridSnapAction_->blockSignals(blocked);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("stage-grid-snap", enabled)));
    }
}

void MainWindow::setStageGuidesVisible(bool visible) {
    EditorPreferences::setViewOption(QStringLiteral("stageGuides"), visible);
    if (stageGuidesShowAction_ && stageGuidesShowAction_->isChecked() != visible) {
        const bool blocked = stageGuidesShowAction_->blockSignals(true);
        stageGuidesShowAction_->setChecked(visible);
        stageGuidesShowAction_->blockSignals(blocked);
    }
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("stage-guides-show", visible)));
    }
}

void MainWindow::setStageGuidesSnapEnabled(bool enabled) {
    EditorPreferences::setViewOption(QStringLiteral("stageGuidesSnap"), enabled);
    if (stageGuidesSnapAction_ && stageGuidesSnapAction_->isChecked() != enabled) {
        const bool blocked = stageGuidesSnapAction_->blockSignals(true);
        stageGuidesSnapAction_->setChecked(enabled);
        stageGuidesSnapAction_->blockSignals(blocked);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(viewToggleStatusText("stage-guides-snap", enabled)));
    }
}

void MainWindow::showGridSettingsDialog() {
    const auto gridText = viewGridSettingsText();
    QDialog dialog(this);
    dialog.setWindowTitle(toQString(gridText.title));
    dialog.setModal(true);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    auto* gridSize = new QSpinBox(&dialog);
    gridSize->setRange(1, 128);
    gridSize->setSuffix(toQString(gridText.pixelSuffix));
    gridSize->setValue(EditorPreferences::preferenceInt(QStringLiteral("stageGrid"), QStringLiteral("gridSize"), 10));
    form->addRow(toQString(gridText.gridSizeLabel), gridSize);

    auto* guideThreshold = new QSpinBox(&dialog);
    guideThreshold->setRange(1, 64);
    guideThreshold->setSuffix(toQString(gridText.pixelSuffix));
    guideThreshold->setValue(EditorPreferences::preferenceInt(QStringLiteral("stageGrid"), QStringLiteral("guideSnapThreshold"), 5));
    form->addRow(toQString(gridText.guideSnapThresholdLabel), guideThreshold);
    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    EditorPreferences::setPreferenceInt(QStringLiteral("stageGrid"), QStringLiteral("gridSize"), gridSize->value());
    EditorPreferences::setPreferenceInt(QStringLiteral("stageGrid"), QStringLiteral("guideSnapThreshold"), guideThreshold->value());
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(gridText.savedStatusText));
    }
}

void MainWindow::refreshStageFrame() {
    if (!stageSummary_) {
        return;
    }
    const auto movie = context_.movie();
    if (!movie || !stagePlayer_) {
        stageSummary_->setPixmap({});
        if (!movie) {
            stageSummary_->setText(toQString(stageWindowNoMovieText()));
        }
        return;
    }

    try {
        stagePlayer_->goToFrame(context_.currentFrame());
        const auto frame = stagePlayer_->frameSnapshot().renderFrame();
        const auto image = bitmapToImage(frame);
        if (!image.isNull()) {
            stageSummary_->setText({});
            auto pixmap = QPixmap::fromImage(image).scaled(stageCanvasFrame_->size(),
                                                           Qt::KeepAspectRatio,
                                                           Qt::FastTransformation);
            const bool showInfo = spriteOverlayInfoAction_ && spriteOverlayInfoAction_->isChecked();
            const bool showPaths = spriteOverlayPathsAction_ && spriteOverlayPathsAction_->isChecked();
            const bool showGridLines = scoreGridLinesAction_
                ? scoreGridLinesAction_->isChecked()
                : EditorPreferences::viewOption(QStringLiteral("scoreGridLines"), true);
            if (showGridLines && movie->stageWidth() > 0 && movie->stageHeight() > 0 && !pixmap.isNull()) {
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setPen(QPen(QColor(64, 160, 255, 72), 1));
                const int gridSize =
                    std::clamp(EditorPreferences::preferenceInt(QStringLiteral("stageGrid"), QStringLiteral("gridSize"), 10),
                               1,
                               128);
                const double scaleX = static_cast<double>(pixmap.width()) / static_cast<double>(movie->stageWidth());
                const double scaleY = static_cast<double>(pixmap.height()) / static_cast<double>(movie->stageHeight());
                for (int x = gridSize; x < movie->stageWidth(); x += gridSize) {
                    const int scaledX = static_cast<int>(std::lround(x * scaleX));
                    painter.drawLine(scaledX, 0, scaledX, pixmap.height());
                }
                for (int y = gridSize; y < movie->stageHeight(); y += gridSize) {
                    const int scaledY = static_cast<int>(std::lround(y * scaleY));
                    painter.drawLine(0, scaledY, pixmap.width(), scaledY);
                }
            }
            if ((showInfo || showPaths) && movie->stageWidth() > 0 && movie->stageHeight() > 0 && !pixmap.isNull()) {
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing, false);
                const double scaleX = static_cast<double>(pixmap.width()) / static_cast<double>(movie->stageWidth());
                const double scaleY = static_cast<double>(pixmap.height()) / static_cast<double>(movie->stageHeight());
                const auto intervals = buildScoreIntervalRows(*movie);
                for (const auto& interval : intervals) {
                    if (!scoreIntervalContainsFrame(interval, context_.currentFrame()) || !interval.hasSpriteData ||
                        interval.width <= 0 || interval.height <= 0) {
                        continue;
                    }
                    const int x = static_cast<int>(std::lround((interval.posX - interval.width / 2.0) * scaleX));
                    const int y = static_cast<int>(std::lround((interval.posY - interval.height / 2.0) * scaleY));
                    const int width = std::max(1, static_cast<int>(std::lround(interval.width * scaleX)));
                    const int height = std::max(1, static_cast<int>(std::lround(interval.height * scaleY)));
                    const QRect rect(x, y, width, height);
                    const QPoint center(static_cast<int>(std::lround(interval.posX * scaleX)),
                                        static_cast<int>(std::lround(interval.posY * scaleY)));

                    if (showPaths) {
                        painter.setPen(QPen(QColor(255, 215, 0), 1, Qt::DashLine));
                        painter.drawLine(center.x() - 6, center.y(), center.x() + 6, center.y());
                        painter.drawLine(center.x(), center.y() - 6, center.x(), center.y() + 6);
                        painter.drawEllipse(center, 3, 3);
                    }
                    if (showInfo) {
                        painter.setPen(QPen(QColor(0, 180, 255), 2));
                        painter.drawRect(rect.adjusted(0, 0, -1, -1));
                        const QString label = toQString(scoreOverlaySpriteLabel(interval.channel,
                                                                                interval.castMember,
                                                                                interval.memberName));
                        const QRect textRect(rect.left(), std::max(0, rect.top() - 18), std::max(120, rect.width()), 18);
                        painter.fillRect(textRect, QColor(0, 0, 0, 170));
                        painter.setPen(Qt::white);
                        painter.drawText(textRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
                    }
                }
            }
            if (stageGuidesShowAction_ && stageGuidesShowAction_->isChecked() && !pixmap.isNull()) {
                QPainter painter(&pixmap);
                painter.setPen(QPen(QColor(255, 64, 180, 180), 1, Qt::DashLine));
                const int centerX = pixmap.width() / 2;
                const int centerY = pixmap.height() / 2;
                painter.drawLine(centerX, 0, centerX, pixmap.height());
                painter.drawLine(0, centerY, pixmap.width(), centerY);
                painter.setPen(QPen(QColor(255, 64, 180, 110), 1, Qt::DotLine));
                painter.drawLine(pixmap.width() / 3, 0, pixmap.width() / 3, pixmap.height());
                painter.drawLine((pixmap.width() * 2) / 3, 0, (pixmap.width() * 2) / 3, pixmap.height());
                painter.drawLine(0, pixmap.height() / 3, pixmap.width(), pixmap.height() / 3);
                painter.drawLine(0, (pixmap.height() * 2) / 3, pixmap.width(), (pixmap.height() * 2) / 3);
            }
            stageSummary_->setPixmap(pixmap);
            stageSummary_->setToolTip(toQString(stageWindowRenderedTooltip(context_.currentPath().filename().string(),
                                                                           context_.currentFrame())));
        }
    } catch (const std::exception& ex) {
        stageSummary_->setPixmap({});
        stageSummary_->setToolTip(QString::fromStdString(ex.what()));
    }
}

void MainWindow::setPaintZoom(double zoom) {
    if (paintOriginalPixmap_.isNull()) {
        return;
    }
    paintZoom_ = std::clamp(zoom, 0.125, 8.0);
    updatePaintPreview();
}

void MainWindow::fitPaintToView() {
    if (paintOriginalPixmap_.isNull() || !paintScrollArea_) {
        return;
    }
    const auto viewportSize = paintScrollArea_->viewport()->size();
    if (viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return;
    }
    const double widthScale = static_cast<double>(viewportSize.width()) / paintOriginalPixmap_.width();
    const double heightScale = static_cast<double>(viewportSize.height()) / paintOriginalPixmap_.height();
    setPaintZoom(std::min(widthScale, heightScale));
}

void MainWindow::updatePaintPreview() {
    if (!paintImageLabel_ || paintOriginalPixmap_.isNull()) {
        return;
    }
    const QSize scaledSize(std::max(1, static_cast<int>(std::lround(paintOriginalPixmap_.width() * paintZoom_))),
                           std::max(1, static_cast<int>(std::lround(paintOriginalPixmap_.height() * paintZoom_))));
    const auto transformMode = paintAntialiasPreview_ ? Qt::SmoothTransformation : Qt::FastTransformation;
    const QPixmap scaledPixmap = paintOriginalPixmap_.scaled(scaledSize, Qt::IgnoreAspectRatio, transformMode);
    QPixmap previewPixmap = scaledPixmap;
    if (paintShowTransparencyGrid_) {
        previewPixmap = QPixmap(scaledSize);
        QPainter painter(&previewPixmap);
        drawCheckerboard(painter, scaledSize);
        painter.drawPixmap(0, 0, scaledPixmap);
    }
    paintImageLabel_->setText({});
    paintImageLabel_->setPixmap(previewPixmap);
    paintImageLabel_->setMinimumSize(scaledSize);
    if (paintStatus_) {
        paintStatus_->setText(toQString(paintWindowZoomStatus(paintBaseStatus_.toStdString(), paintZoom_)));
    }
}

void MainWindow::configureStagePlayerLocalHttpRoot() {
    if (!stagePlayer_) {
        return;
    }

    const bool networkEnabled =
        EditorPreferences::preferenceBool(QStringLiteral("network"), QStringLiteral("enableNetwork"), false);
    const bool remoteAssetsAllowed =
        EditorPreferences::preferenceBool(QStringLiteral("network"), QStringLiteral("allowRemoteAssets"), false);
    stagePlayer_->netManager().setRemoteFetchEnabled(networkEnabled && remoteAssetsAllowed);
    if (EditorPreferences::preferenceBool(QStringLiteral("network"), QStringLiteral("logNetwork"), false)) {
        stagePlayer_->netManager().setTaskCallback([this](const auto& task) {
            const auto methodName = ::libreshockwave::player::net::name(task.method());
            const QString message = task.result().has_value()
                ? toQString(stageWindowNetworkTaskStatusText(methodName,
                                                             task.originalUrl(),
                                                             task.result()->size()))
                : toQString(stageWindowNetworkTaskFailedStatusText(methodName,
                                                                   task.originalUrl(),
                                                                   task.errorMessage().value_or("error")));
            QMetaObject::invokeMethod(this, [this, message] {
                if (statusLabel_) {
                    statusLabel_->setText(message);
                }
            }, Qt::QueuedConnection);
        });
    } else {
        stagePlayer_->netManager().setTaskCallback({});
    }

    if (context_.currentPath().empty()) {
        return;
    }
    const auto root = localHttpRootForMoviePath(context_.currentPath());
    if (root.empty()) {
        return;
    }

    stagePlayer_->netManager().setLocalHttpRoot(root);
    if (statusLabel_) {
        statusLabel_->setText(toQString(stageWindowLocalHttpRootStatusText(root)));
    }
}

void MainWindow::syncExternalParamsToStagePlayer() {
    if (!stagePlayer_) {
        return;
    }
    const auto rows = externalParamRows(context_.externalParams());
    stagePlayer_->setExternalParams(externalParamsForRuntime(rows));
}

void MainWindow::attachDebugControllerToStagePlayer() {
    if (!stagePlayer_ || !debugController_) {
        return;
    }
    debugController_->reset();
    stagePlayer_->setDebugController(debugController_);
    loadSavedBreakpoints();
}

void MainWindow::loadSavedBreakpoints() {
    if (!debugController_ || context_.currentPath().empty()) {
        return;
    }

    const auto serialized = EditorPreferences::movieBreakpoints(QString::fromStdString(context_.currentPath().string()));
    debugController_->deserializeBreakpoints(serialized.toStdString());
}

void MainWindow::saveCurrentBreakpoints() {
    if (!debugController_ || context_.currentPath().empty()) {
        return;
    }

    const auto serialized = QString::fromStdString(debugController_->serializeBreakpoints());
    EditorPreferences::setMovieBreakpoints(QString::fromStdString(context_.currentPath().string()), serialized);
}

void MainWindow::clearSavedBreakpoints() {
    if (context_.currentPath().empty()) {
        return;
    }

    EditorPreferences::clearMovieBreakpoints(QString::fromStdString(context_.currentPath().string()));
}

bool MainWindow::handlePaintMouseEvent(QObject* watched, QEvent* event) {
    if (watched != paintImageLabel_ || !stagePlayer_ || paintEditorMemberNum_ <= 0 ||
        paintOriginalImage_.isNull()) {
        return false;
    }
    if (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonRelease) {
        return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() != Qt::LeftButton) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease && mouseEvent->button() != Qt::LeftButton) {
        return false;
    }

    const double zoom = std::max(0.125, paintZoom_);
    const int x = std::clamp(static_cast<int>(std::floor(mouseEvent->position().x() / zoom)),
                             0,
                             paintOriginalImage_.width() - 1);
    const int y = std::clamp(static_cast<int>(std::floor(mouseEvent->position().y() / zoom)),
                             0,
                             paintOriginalImage_.height() - 1);
    const QString selectedTool =
        EditorPreferences::preferenceString(QStringLiteral("paint"), QStringLiteral("selectedTool"), QStringLiteral("Pencil"));

    if (selectedTool == QStringLiteral("Line") ||
        selectedTool == QStringLiteral("Rect") ||
        selectedTool == QStringLiteral("Oval")) {
        if (event->type() == QEvent::MouseButtonPress) {
            paintDragActive_ = true;
            paintDragStart_ = QPoint(x, y);
            paintDragTool_ = selectedTool;
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            return paintDragActive_;
        }
        if (!paintDragActive_ || paintDragTool_ != selectedTool) {
            return false;
        }
    } else if (event->type() != QEvent::MouseButtonPress) {
        return false;
    }

    QImage edited = paintOriginalImage_.convertToFormat(QImage::Format_ARGB32);
    const int brushSize = selectedTool == QStringLiteral("Pencil") ? 1 : paintBrushSize_;
    const QRgb color = selectedTool == QStringLiteral("Eraser")
        ? qRgba(0, 0, 0, 0)
        : qRgba(paintDrawColor_.red(), paintDrawColor_.green(), paintDrawColor_.blue(), 255);
    if (selectedTool == QStringLiteral("Fill")) {
        edited.fill(color);
    } else if (selectedTool == QStringLiteral("Pencil") ||
               selectedTool == QStringLiteral("Brush") ||
               selectedTool == QStringLiteral("Eraser")) {
        QPainter painter(&edited);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgba(color));
        const int half = brushSize / 2;
        painter.drawRect(QRect(x - half, y - half, std::max(1, brushSize), std::max(1, brushSize)));
    } else if (selectedTool == QStringLiteral("Line") ||
               selectedTool == QStringLiteral("Rect") ||
               selectedTool == QStringLiteral("Oval")) {
        QPainter painter(&edited);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing, paintAntialiasPreview_);
        painter.setPen(QPen(QColor::fromRgba(color), std::max(1, paintBrushSize_)));
        painter.setBrush(Qt::NoBrush);
        const QRect rect(QPoint(std::min(paintDragStart_.x(), x), std::min(paintDragStart_.y(), y)),
                         QPoint(std::max(paintDragStart_.x(), x), std::max(paintDragStart_.y(), y)));
        if (selectedTool == QStringLiteral("Line")) {
            painter.drawLine(paintDragStart_, QPoint(x, y));
        } else if (selectedTool == QStringLiteral("Rect")) {
            painter.drawRect(rect);
        } else {
            painter.drawEllipse(rect);
        }
        paintDragActive_ = false;
        paintDragTool_.clear();
    } else {
        return false;
    }

    const int runtimeCastLib = paintEditorCastLib_ > 0 ? paintEditorCastLib_ : 1;
    auto bitmap = imageToRuntimeBitmap(edited);
    const bool applied = bitmap != nullptr &&
        stagePlayer_->castLibManager().setMemberProp(runtimeCastLib,
                                                     paintEditorMemberNum_,
                                                     "image",
                                                     lingo::Datum::imageRef(std::move(bitmap)));
    if (!applied) {
        const QString status = toQString(paintWindowRuntimeEditFailedStatus(selectedTool.toStdString()));
        if (paintStatus_) {
            paintStatus_->setText(status);
        }
        if (statusLabel_) {
            statusLabel_->setText(status.trimmed());
        }
        return true;
    }

    paintOriginalImage_ = edited;
    paintOriginalPixmap_ = QPixmap::fromImage(paintOriginalImage_);
    updatePaintPreview();
    castThumbnailCache_.clear();
    refreshStageFrame();
    const QString status = toQString(paintWindowRuntimeEditStatus(selectedTool.toStdString(), x, y));
    if (paintStatus_) {
        paintStatus_->setText(status);
    }
    if (statusLabel_) {
        statusLabel_->setText(status.trimmed());
    }
    return true;
}

bool MainWindow::applyVectorShapeTool(const QString& toolName) {
    if (!stagePlayer_ || vectorShapeEditorMemberNum_ <= 0) {
        return false;
    }

    std::optional<cast::ShapeType> targetType;
    std::string symbolName;
    if (toolName == QStringLiteral("Line")) {
        targetType = cast::ShapeType::Line;
        symbolName = "line";
    } else if (toolName == QStringLiteral("Rect")) {
        targetType = cast::ShapeType::Rect;
        symbolName = "rect";
    } else if (toolName == QStringLiteral("Ellipse")) {
        targetType = cast::ShapeType::Oval;
        symbolName = "oval";
    } else {
        return false;
    }

    const int runtimeCastLib = vectorShapeEditorCastLib_ > 0 ? vectorShapeEditorCastLib_ : 1;
    const bool applied = stagePlayer_->castLibManager().setMemberProp(runtimeCastLib,
                                                                      vectorShapeEditorMemberNum_,
                                                                      "shapeType",
                                                                      lingo::Datum::symbol(symbolName));
    const QString status = applied
        ? toQString(vectorShapeRuntimeEditStatus(toolName.toStdString(), *targetType))
        : toQString(vectorShapeRuntimeEditFailedStatus(toolName.toStdString()));
    if (applied) {
        castThumbnailCache_.clear();
        loadCastMemberIntoPanel(QStringLiteral("vector-shape"), {}, runtimeCastLib, vectorShapeEditorMemberNum_);
        refreshStageFrame();
    }
    if (vectorShapeStatus_) {
        vectorShapeStatus_->setText(status);
    }
    if (statusLabel_) {
        statusLabel_->setText(status.trimmed());
    }
    return true;
}

bool MainWindow::handleStageMouseEvent(QObject* watched, QEvent* event) {
    if (!stagePlayer_ || !stageCanvasFrame_ || !stageSummary_) {
        return false;
    }
    if (watched != stageCanvasFrame_ && watched != stageSummary_) {
        return false;
    }
    if (event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonRelease) {
        return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    const auto* sourceWidget = qobject_cast<QWidget*>(watched);
    if (!sourceWidget) {
        return false;
    }

    const QPoint local = stageCanvasFrame_->mapFrom(sourceWidget, mouseEvent->pos());
    const auto movie = context_.movie();
    const int logicalWidth = movie && movie->stageWidth() > 0 ? movie->stageWidth() : stageWindowDefaultWidth();
    const int logicalHeight = movie && movie->stageHeight() > 0 ? movie->stageHeight() : stageWindowDefaultHeight();
    const double scale = std::max(0.25, static_cast<double>(stageZoomPercent_) / 100.0);
    auto stagePoint = stageWindowClampedPoint(static_cast<int>(std::lround(local.x() / scale)),
                                             static_cast<int>(std::lround(local.y() / scale)),
                                             logicalWidth,
                                             logicalHeight);
    const bool rightButton = mouseEvent->button() == Qt::RightButton ||
                             mouseEvent->buttons().testFlag(Qt::RightButton);
    const auto tools = toolPaletteTools();
    if (activeToolId_ > 0 && activeToolId_ < static_cast<int>(tools.size())) {
        if (stageGridSnapAction_ && stageGridSnapAction_->isChecked()) {
            const int gridSize =
                std::clamp(EditorPreferences::preferenceInt(QStringLiteral("stageGrid"), QStringLiteral("gridSize"), 10),
                           1,
                           128);
            stagePoint.x =
                std::clamp(((stagePoint.x + gridSize / 2) / gridSize) * gridSize, 0, std::max(0, logicalWidth - 1));
            stagePoint.y =
                std::clamp(((stagePoint.y + gridSize / 2) / gridSize) * gridSize, 0, std::max(0, logicalHeight - 1));
        }
        if (stageGuidesSnapAction_ && stageGuidesSnapAction_->isChecked()) {
            const int threshold = std::clamp(EditorPreferences::preferenceInt(QStringLiteral("stageGrid"),
                                                                              QStringLiteral("guideSnapThreshold"),
                                                                              5),
                                             1,
                                             64);
            const std::array<int, 3> xGuides{{logicalWidth / 3, logicalWidth / 2, (logicalWidth * 2) / 3}};
            const std::array<int, 3> yGuides{{logicalHeight / 3, logicalHeight / 2, (logicalHeight * 2) / 3}};
            for (const int guide : xGuides) {
                if (std::abs(stagePoint.x - guide) <= threshold) {
                    stagePoint.x = guide;
                    break;
                }
            }
            for (const int guide : yGuides) {
                if (std::abs(stagePoint.y - guide) <= threshold) {
                    stagePoint.y = guide;
                    break;
                }
            }
        }
        const auto toolName = tools[static_cast<std::size_t>(activeToolId_)];
        if (event->type() == QEvent::MouseButtonPress) {
            stageCanvasFrame_->setFocus(Qt::MouseFocusReason);
            return moveSelectedStageSprite(toolName, stagePoint.x, stagePoint.y);
        }
        if (statusLabel_) {
            statusLabel_->setText(toQString(toolPaletteStageInteractionText(toolName, stagePoint.x, stagePoint.y)));
        }
        if (activeToolLabel_) {
            activeToolLabel_->setText(toQString(toolPaletteStageToolText(toolName, stagePoint.x, stagePoint.y)));
        }
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        stagePlayer_->inputHandler().onMouseMove(stagePoint.x, stagePoint.y);
    } else if (event->type() == QEvent::MouseButtonPress) {
        stageCanvasFrame_->setFocus(Qt::MouseFocusReason);
        stagePlayer_->inputHandler().onMouseDown(stagePoint.x, stagePoint.y, rightButton);
    } else {
        stagePlayer_->inputHandler().onMouseUp(stagePoint.x, stagePoint.y, rightButton);
    }
    refreshStageFrame();
    return true;
}

bool MainWindow::moveSelectedStageSprite(std::string_view toolName, int x, int y) {
    if (!stagePlayer_) {
        return false;
    }
    const auto selection = context_.selection();
    if ((selection.type != SelectionType::ScoreCell && selection.type != SelectionType::Sprite) ||
        selection.channel <= 0) {
        const QString status = toQString(toolPaletteStageNoSpriteText(toolName));
        if (statusLabel_) {
            statusLabel_->setText(status);
        }
        return true;
    }

    const bool applied = stagePlayer_->spriteProperties().setSpriteProp(selection.channel,
                                                                       "loc",
                                                                       lingo::Datum::intPoint(x, y));
    const QString status = applied
        ? toQString(toolPaletteStageSpriteMoveText(toolName, selection.channel, x, y))
        : toQString(toolPaletteStageSpriteMoveFailedText(toolName, selection.channel));
    if (applied) {
        if (selection.type != SelectionType::Sprite) {
            context_.selectSprite(selection.channel, selection.frame > 0 ? selection.frame : context_.currentFrame());
        }
        refreshStageFrame();
        updatePropertyInspectorForSelection(context_.selection());
        if (activeToolLabel_) {
            activeToolLabel_->setText(toQString(toolPaletteStageToolText(toolName, x, y)));
        }
    }
    if (statusLabel_) {
        statusLabel_->setText(status);
    }
    return true;
}

bool MainWindow::nudgeSelectedStageSprite(std::string_view toolName,
                                          std::string_view direction,
                                          int deltaX,
                                          int deltaY,
                                          int pixels) {
    if (!stagePlayer_) {
        return false;
    }
    const auto selection = context_.selection();
    if ((selection.type != SelectionType::ScoreCell && selection.type != SelectionType::Sprite) ||
        selection.channel <= 0) {
        const QString status = toQString(toolPaletteNudgeNoSpriteText(toolName, direction, pixels));
        if (statusLabel_) {
            statusLabel_->setText(status);
        }
        return true;
    }

    const auto loc = stagePlayer_->spriteProperties().getSpriteProp(selection.channel, "loc");
    const auto* point = loc.asIntPoint();
    const int currentX = point ? point->x : 0;
    const int currentY = point ? point->y : 0;
    const bool applied = stagePlayer_->spriteProperties().setSpriteProp(selection.channel,
                                                                       "loc",
                                                                       lingo::Datum::intPoint(currentX + deltaX,
                                                                                              currentY + deltaY));
    const auto updated = stagePlayer_->spriteProperties().getSpriteProp(selection.channel, "loc");
    const auto* updatedPoint = updated.asIntPoint();
    const int x = updatedPoint ? updatedPoint->x : currentX + deltaX;
    const int y = updatedPoint ? updatedPoint->y : currentY + deltaY;
    const QString status = applied
        ? toQString(toolPaletteNudgeSpriteText(toolName, direction, pixels, selection.channel, x, y))
        : toQString(toolPaletteNudgeSpriteFailedText(toolName, direction, pixels, selection.channel));
    if (applied) {
        if (selection.type != SelectionType::Sprite) {
            context_.selectSprite(selection.channel, selection.frame > 0 ? selection.frame : context_.currentFrame());
        }
        refreshStageFrame();
        updatePropertyInspectorForSelection(context_.selection());
        if (activeToolLabel_) {
            activeToolLabel_->setText(toQString(toolPaletteNudgeToolText(toolName, direction, pixels)));
        }
    }
    if (statusLabel_) {
        statusLabel_->setText(status);
    }
    return true;
}

bool MainWindow::handleStageKeyEvent(QObject* watched, QEvent* event) {
    if (!stagePlayer_ || !stageCanvasFrame_ || !stageSummary_) {
        return false;
    }
    if (watched != stageCanvasFrame_ && watched != stageSummary_) {
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->isAutoRepeat()) {
        return true;
    }

    const auto modifiers = keyEvent->modifiers();
    const int directorKeyCode = stageWindowDirectorKeyCodeFromBrowserCode(browserKeyCodeFromQtKey(keyEvent->key()));
    const std::string keyText = keyEvent->text().toStdString();
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    const auto tools = toolPaletteTools();
    if (activeToolId_ > 0 && activeToolId_ < static_cast<int>(tools.size())) {
        std::optional<ToolNudgeDirection> direction;
        if (keyEvent->key() == Qt::Key_Left) {
            direction = ToolNudgeDirection::Left;
        } else if (keyEvent->key() == Qt::Key_Right) {
            direction = ToolNudgeDirection::Right;
        } else if (keyEvent->key() == Qt::Key_Up) {
            direction = ToolNudgeDirection::Up;
        } else if (keyEvent->key() == Qt::Key_Down) {
            direction = ToolNudgeDirection::Down;
        }
        if (direction.has_value()) {
            if (event->type() == QEvent::KeyPress) {
                const int nudgePixels =
                    std::clamp(EditorPreferences::preferenceInt(QStringLiteral("sprite"), QStringLiteral("nudgePixels"), 1),
                               1,
                               100);
                const auto toolName = tools[static_cast<std::size_t>(activeToolId_)];
                const auto directionText = toolPaletteNudgeDirectionText(*direction);
                int deltaX = 0;
                int deltaY = 0;
                switch (*direction) {
                    case ToolNudgeDirection::Left:
                        deltaX = -nudgePixels;
                        break;
                    case ToolNudgeDirection::Right:
                        deltaX = nudgePixels;
                        break;
                    case ToolNudgeDirection::Up:
                        deltaY = -nudgePixels;
                        break;
                    case ToolNudgeDirection::Down:
                        deltaY = nudgePixels;
                        break;
                }
                (void)nudgeSelectedStageSprite(toolName, directionText, deltaX, deltaY, nudgePixels);
            }
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        stagePlayer_->inputHandler().onKeyDown(directorKeyCode, keyText, shift, ctrl, alt);
    } else {
        stagePlayer_->inputHandler().onKeyUp(directorKeyCode, keyText, shift, ctrl, alt);
    }
    refreshStageFrame();
    return true;
}

void MainWindow::advancePlaybackFrame() {
    const auto movie = context_.movie();
    if (!movie || !context_.isPlaying()) {
        if (playbackTimer_) {
            playbackTimer_->stop();
        }
        return;
    }

    const int frameCount = movie->scoreChunk() ? std::max(1, movie->scoreChunk()->getFrameCount()) : 1;
    if (context_.currentFrame() >= frameCount) {
        if (context_.loopPlayback()) {
            context_.rewind();
            context_.play();
        } else {
            context_.stop();
        }
        return;
    }

    context_.stepForward();
}

void MainWindow::updateWindowTitle() {
    if (context_.currentPath().empty()) {
        setWindowTitle(toQString(mainWindowShellText().defaultTitle));
        return;
    }
    setWindowTitle(toQString(mainWindowTitleForMovie(context_.currentPath().filename().string())));
}

void MainWindow::updateScriptEditorPreview() {
    if (!scriptPreview_ || !scriptHandlerSelector_) {
        return;
    }
    const auto bytecodePreviews = scriptSelector_ ? scriptSelector_->currentData().toStringList() : QStringList{};
    const auto lingoPreviews = scriptSelector_ ? scriptSelector_->currentData(Qt::UserRole + 3).toStringList() : QStringList{};
    const bool showLingo = scriptLingoToggle_ && scriptLingoToggle_->isChecked() && !lingoPreviews.isEmpty();
    QStringList handlerPreviews = showLingo ? lingoPreviews : bytecodePreviews;
    if (!showLingo) {
        for (QString& handlerPreview : handlerPreviews) {
            handlerPreview = bytecodePreviewWithAnnotationPreference(handlerPreview);
        }
    }
    const int previousHandlerIndex = scriptHandlerSelector_->currentIndex();
    const bool handlerSignalsBlocked = scriptHandlerSelector_->blockSignals(true);
    scriptHandlerSelector_->clear();
    if (bytecodePreviews.isEmpty()) {
        scriptHandlerSelector_->addItem(toQString(scriptEditorNoHandlersText()));
    } else {
        scriptHandlerSelector_->addItem(toQString(scriptEditorAllHandlersText()));
        for (const auto& handlerPreview : bytecodePreviews) {
            scriptHandlerSelector_->addItem(handlerPreview.section(QLatin1Char('\n'), 0, 0));
        }
    }
    if (previousHandlerIndex >= 0 && previousHandlerIndex < scriptHandlerSelector_->count()) {
        scriptHandlerSelector_->setCurrentIndex(previousHandlerIndex);
    }
    scriptHandlerSelector_->blockSignals(handlerSignalsBlocked);

    if (bytecodePreviews.isEmpty()) {
        scriptPreview_->setPlainText(toQString(scriptEditorNoHandlersText()));
        return;
    }
    const int handlerIndex = scriptHandlerSelector_->currentIndex();
    if (handlerIndex > 0 && handlerIndex - 1 < handlerPreviews.size()) {
        scriptPreview_->setPlainText(handlerPreviews.at(handlerIndex - 1));
    } else {
        scriptPreview_->setPlainText(handlerPreviews.join(QStringLiteral("\n")));
    }
}

void MainWindow::refreshBytecodeDebuggerScriptFilter() {
    if (!debugScriptSelector_) {
        return;
    }

    const QString previousSelection = debugScriptSelector_->currentText();
    const QString filter = debugScriptFilter_ ? debugScriptFilter_->text().trimmed() : QString{};
    const bool scriptSignalsBlocked = debugScriptSelector_->blockSignals(true);
    debugScriptSelector_->clear();

    int previousIndex = -1;
    for (qsizetype index = 0; index < debugScriptLabels_.size(); ++index) {
        const QString& label = debugScriptLabels_.at(index);
        if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        debugScriptSelector_->addItem(label, debugScriptHandlerPreviews_.at(index));
        if (index >= 0 && index < debugScriptLingoHandlerPreviews_.size()) {
            debugScriptSelector_->setItemData(debugScriptSelector_->count() - 1,
                                              debugScriptLingoHandlerPreviews_.at(index),
                                              Qt::UserRole + 3);
        }
        debugScriptSelector_->setItemData(debugScriptSelector_->count() - 1, static_cast<int>(index), Qt::UserRole + 1);
        debugScriptSelector_->setItemData(debugScriptSelector_->count() - 1, debugScriptIds_.at(index), Qt::UserRole + 2);
        if (label == previousSelection) {
            previousIndex = debugScriptSelector_->count() - 1;
        }
    }

    if (debugScriptSelector_->count() == 0) {
        debugScriptSelector_->addItem(toQString(scriptEditorNoScriptsText()));
    } else if (previousIndex >= 0) {
        debugScriptSelector_->setCurrentIndex(previousIndex);
    }

    debugScriptSelector_->blockSignals(scriptSignalsBlocked);
    updateBytecodeDebuggerPreview();
}

void MainWindow::updateBytecodeDebuggerPreview() {
    if (!debugBytecodeList_ || !debugHandlerSelector_ || !debugScriptSelector_) {
        return;
    }

    const auto handlerPreviews = debugScriptSelector_->currentData().toStringList();
    const auto lingoPreviews = debugScriptSelector_->currentData(Qt::UserRole + 3).toStringList();
    const QString handlerFilter = debugHandlerFilter_ ? debugHandlerFilter_->text().trimmed() : QString{};
    QStringList filteredHandlerPreviews;
    QStringList filteredLingoPreviews;
    for (qsizetype index = 0; index < handlerPreviews.size(); ++index) {
        const auto& handlerPreview = handlerPreviews.at(index);
        const QString handlerName = handlerPreview.section(QLatin1Char('\n'), 0, 0);
        if (handlerFilter.isEmpty() || handlerName.contains(handlerFilter, Qt::CaseInsensitive)) {
            filteredHandlerPreviews.push_back(handlerPreview);
            if (index < lingoPreviews.size()) {
                filteredLingoPreviews.push_back(lingoPreviews.at(index));
            }
        }
    }
    const bool showLingo = debugLingoToggle_ && debugLingoToggle_->isChecked() && !filteredLingoPreviews.isEmpty();

    const int previousHandlerIndex = debugHandlerSelector_->currentIndex();
    const bool handlerSignalsBlocked = debugHandlerSelector_->blockSignals(true);
    debugHandlerSelector_->clear();
    if (filteredHandlerPreviews.isEmpty()) {
        debugHandlerSelector_->addItem(toQString(scriptEditorNoHandlersText()));
    } else {
        debugHandlerSelector_->addItem(toQString(scriptEditorAllHandlersText()), filteredHandlerPreviews.join(QStringLiteral("\n")));
        for (const auto& handlerPreview : filteredHandlerPreviews) {
            debugHandlerSelector_->addItem(handlerPreview.section(QLatin1Char('\n'), 0, 0), handlerPreview);
        }
    }
    if (previousHandlerIndex >= 0 && previousHandlerIndex < debugHandlerSelector_->count()) {
        debugHandlerSelector_->setCurrentIndex(previousHandlerIndex);
    }
    debugHandlerSelector_->blockSignals(handlerSignalsBlocked);

    if (debugHandlerDetailsButton_) {
        debugHandlerDetailsButton_->setEnabled(!filteredHandlerPreviews.isEmpty() &&
                                               debugHandlerSelector_->currentIndex() > 0);
    }
    if (debugHandlerLabel_) {
        debugHandlerLabel_->setText(filteredHandlerPreviews.isEmpty() ? toQString(debugInitialHandlerText())
                                                                     : debugHandlerSelector_->currentText());
    }
    if (debugLingoToggle_) {
        debugLingoToggle_->setEnabled(!filteredLingoPreviews.isEmpty());
        const auto browserText = debugBrowserText();
        debugLingoToggle_->setText(toQString(showLingo ? browserText.bytecodeToggleText : browserText.lingoToggleText));
    }
    const int scriptId = currentDebugScriptId();
    QStringList decoratedPreviews;
    if (showLingo) {
        decoratedPreviews = filteredLingoPreviews;
    } else {
        for (const auto& handlerPreview : filteredHandlerPreviews) {
            decoratedPreviews.push_back(decorateDebugHandlerPreview(handlerPreview,
                                                                    scriptId,
                                                                    handlerPreview.section(QLatin1Char('\n'), 0, 0)));
        }
    }
    setDebugBytecodeListing(filteredHandlerPreviews.isEmpty()
                                ? toQString(debugBytecodePlaceholderText())
                                : decoratedPreviews.join(QStringLiteral("\n")));
}

void MainWindow::setDebugBytecodeListing(const QString& text) {
    if (!debugBytecodeList_) {
        return;
    }
    debugBytecodeList_->clear();
    const QStringList lines = bytecodePreviewWithAnnotationPreference(text).split(QLatin1Char('\n'));
    for (QString line : lines) {
        const QString originalLine = line;
        const QString trimmed = line.trimmed();
        const bool current = trimmed.startsWith(QStringLiteral("=>"));
        if (current) {
            line = line.mid(line.indexOf(QStringLiteral("=>")) + 2).trimmed();
        }
        bool breakpoint = line.trimmed().startsWith(QStringLiteral("B "));
        if (breakpoint) {
            const int bpIndex = line.indexOf(QStringLiteral("B "));
            line.remove(bpIndex, 2);
        }

        const int bracket = line.indexOf(QLatin1Char('['));
        const int close = line.indexOf(QLatin1Char(']'), bracket + 1);
        std::optional<int> offset;
        if (bracket >= 0 && close > bracket + 1) {
            bool ok = false;
            const int parsedOffset = line.mid(bracket + 1, close - bracket - 1).trimmed().toInt(&ok);
            if (ok) {
                offset = parsedOffset;
            }
        }

        QString target;
        const QStringList callOpcodes{
            QStringLiteral("LOCAL_CALL"),
            QStringLiteral("EXT_CALL"),
            QStringLiteral("OBJ_CALL"),
            QStringLiteral("OBJ_CALL_V4"),
            QStringLiteral("TELL_CALL"),
        };
        for (const auto& opcode : callOpcodes) {
            const int opcodePos = line.indexOf(opcode);
            if (opcodePos < 0) {
                continue;
            }
            const int targetOpen = line.indexOf(QLatin1Char('['), opcodePos + opcode.size());
            const int targetClose = line.indexOf(QLatin1Char(']'), targetOpen + 1);
            if (targetOpen >= 0 && targetClose > targetOpen + 1) {
                target = line.mid(targetOpen + 1, targetClose - targetOpen - 1).trimmed();
            }
            break;
        }

        bool breakpointEnabled = breakpoint;
        if (debugController_ && offset.has_value()) {
            const int scriptId = currentDebugScriptId();
            const QString handlerName = currentDebugHandlerName();
            if (scriptId >= 0 && !handlerName.isEmpty()) {
                const auto nativeBreakpoint = debugController_->getBreakpoint(scriptId, handlerName.toStdString(), *offset);
                if (nativeBreakpoint.has_value()) {
                    breakpoint = true;
                    breakpointEnabled = nativeBreakpoint->enabled;
                }
            }
        }

        auto* item = new QListWidgetItem(toQString(debugBytecodeListItemText(line.trimmed().toStdString(),
                                                                             breakpoint,
                                                                             breakpointEnabled,
                                                                             current)));
        item->setData(kBytecodeOffsetRole, offset.has_value() ? QVariant(*offset) : QVariant());
        item->setData(kBytecodeCallTargetRole, target);
        item->setToolTip(originalLine);
        QFont itemFont = debugBytecodeList_->font();
        if (!target.isEmpty() && debugHandlerExists(target)) {
            item->setForeground(QBrush(Qt::blue));
            itemFont.setUnderline(true);
        }
        if (breakpoint) {
            item->setForeground(QBrush(breakpointEnabled ? QColor(170, 0, 0) : QColor(120, 120, 120)));
        }
        item->setFont(itemFont);
        if (current) {
            item->setBackground(QBrush(QColor(255, 255, 200)));
        }
        debugBytecodeList_->addItem(item);
    }
}

void MainWindow::selectDebugInstructionOffset(int offset) {
    if (!debugBytecodeList_) {
        return;
    }
    for (int row = 0; row < debugBytecodeList_->count(); ++row) {
        auto* item = debugBytecodeList_->item(row);
        if (!item) {
            continue;
        }
        bool ok = false;
        const int itemOffset = item->data(kBytecodeOffsetRole).toInt(&ok);
        if (ok && itemOffset == offset) {
            debugBytecodeList_->setCurrentItem(item);
            debugBytecodeList_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            debugBytecodeList_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void MainWindow::showBytecodeHandlerDetails() {
    if (!debugHandlerSelector_) {
        return;
    }

    const QString handlerPreview = debugHandlerSelector_->currentData().toString();
    if (handlerPreview.isEmpty()) {
        return;
    }

    const auto detailsText = debugDetailsText();
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(toQString(debugHandlerDetailsTitle(debugHandlerSelector_->currentText().toStdString())));
    dialog->resize(640, 520);

    auto* layout = new QVBoxLayout(dialog);
    auto* tabs = new QTabWidget(dialog);

    auto* overview = new QPlainTextEdit(tabs);
    overview->setReadOnly(true);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(11);
    overview->setFont(monoFont);
    overview->setPlainText(toQString(debugHandlerDetailsOverviewText(
        debugScriptSelector_ ? debugScriptSelector_->currentText().toStdString() : std::string{},
        debugHandlerSelector_->currentText().toStdString(),
        handlerPreview.section(QLatin1Char('\n'), 0, 3).toStdString())));

    auto* bytecode = new QPlainTextEdit(tabs);
    bytecode->setReadOnly(true);
    bytecode->setFont(monoFont);
    bytecode->setPlainText(handlerPreview);

    tabs->addTab(overview, toQString(detailsText.overviewTabText));
    tabs->addTab(bytecode, toQString(detailsText.bytecodeTabText));
    bool hasScriptIndex = false;
    const int scriptIndex = debugScriptSelector_ ? debugScriptSelector_->currentData(Qt::UserRole + 1).toInt(&hasScriptIndex) : -1;
    if (hasScriptIndex && scriptIndex >= 0 && scriptIndex < debugScriptLiterals_.size() &&
        !debugScriptLiterals_.at(scriptIndex).isEmpty()) {
        auto* literals = new QPlainTextEdit(tabs);
        literals->setReadOnly(true);
        literals->setFont(monoFont);
        literals->setPlainText(debugScriptLiterals_.at(scriptIndex).join(QStringLiteral("\n")));
        tabs->addTab(literals, toQString(detailsText.literalsTabText));
    }
    if (hasScriptIndex && scriptIndex >= 0 && scriptIndex < debugScriptProperties_.size() &&
        !debugScriptProperties_.at(scriptIndex).isEmpty()) {
        auto* properties = new QPlainTextEdit(tabs);
        properties->setReadOnly(true);
        properties->setFont(monoFont);
        properties->setPlainText(debugScriptProperties_.at(scriptIndex).join(QStringLiteral("\n")));
        tabs->addTab(properties, toQString(detailsText.propertiesTabText));
    }
    if (hasScriptIndex && scriptIndex >= 0 && scriptIndex < debugScriptGlobals_.size() &&
        !debugScriptGlobals_.at(scriptIndex).isEmpty()) {
        auto* globals = new QPlainTextEdit(tabs);
        globals->setReadOnly(true);
        globals->setFont(monoFont);
        globals->setPlainText(debugScriptGlobals_.at(scriptIndex).join(QStringLiteral("\n")));
        tabs->addTab(globals, toQString(detailsText.globalsTabText));
    }
    layout->addWidget(tabs, 1);

    auto* closeButton = new QPushButton(toQString(detailsText.closeButtonText), dialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    auto* buttons = new QWidget(dialog);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);
    layout->addWidget(buttons);

    dialog->show();
}

void MainWindow::showBytecodeContextMenu(const QPoint& pos) {
    if (!debugBytecodeList_) {
        return;
    }
    if (auto* item = debugBytecodeList_->itemAt(pos)) {
        debugBytecodeList_->setCurrentItem(item);
    }
    QMenu menu(debugBytecodeList_);
    const QString target = selectedDebugCallTargetName();
    const bool hasNavigationTarget = !target.isEmpty() && debugHandlerExists(target);
    bool addedNavigationSeparator = false;
    for (const auto& command : debugBytecodeContextCommandSpecs()) {
        if (command.requiresNavigationTarget && !addedNavigationSeparator) {
            menu.addSeparator();
            addedNavigationSeparator = true;
        }
        auto* action = menu.addAction(toQString(command.menuText));
        if (command.requiresNavigationTarget) {
            action->setEnabled(hasNavigationTarget);
        } else {
            action->setEnabled(debugController_ && selectedDebugInstructionOffset().has_value());
        }

        const auto id = command.id;
        if (id == "toggle-breakpoint") {
            connect(action, &QAction::triggered, this, [this] { toggleSelectedBytecodeBreakpoint(); });
        } else if (id == "enable-disable-breakpoint") {
            connect(action, &QAction::triggered, this, [this] { toggleSelectedBytecodeBreakpointEnabled(); });
        } else if (id == "go-to-definition") {
            connect(action, &QAction::triggered, this, [this, target] { navigateToDebugHandler(target, false); });
        } else if (id == "view-handler-details") {
            connect(action, &QAction::triggered, this, [this, target] { navigateToDebugHandler(target, true); });
        }
    }
    menu.exec(debugBytecodeList_->mapToGlobal(pos));
}

void MainWindow::toggleSelectedBytecodeBreakpoint() {
    if (!debugController_) {
        return;
    }
    const int scriptId = currentDebugScriptId();
    const QString handlerName = currentDebugHandlerName();
    const auto offset = selectedDebugInstructionOffset();
    if (scriptId < 0 || handlerName.isEmpty() || !offset.has_value()) {
        return;
    }
    const bool added = debugController_->toggleBreakpoint(scriptId, handlerName.toStdString(), *offset);
    (void)added;
    updateBytecodeDebuggerPreview();
    if (statusLabel_) {
        statusLabel_->setText(toQString(debugBreakpointToggledStatusText(*offset)));
    }
}

void MainWindow::toggleSelectedBytecodeBreakpointEnabled() {
    if (!debugController_) {
        return;
    }
    const int scriptId = currentDebugScriptId();
    const QString handlerName = currentDebugHandlerName();
    const auto offset = selectedDebugInstructionOffset();
    if (scriptId < 0 || handlerName.isEmpty() || !offset.has_value()) {
        return;
    }
    const auto breakpoint = debugController_->toggleBreakpointEnabled(scriptId, handlerName.toStdString(), *offset);
    (void)breakpoint;
    updateBytecodeDebuggerPreview();
    if (statusLabel_) {
        statusLabel_->setText(toQString(debugBreakpointEnabledToggledStatusText(*offset)));
    }
}

void MainWindow::navigateToDebugHandler(const QString& handlerName, bool showDetails) {
    if (!debugScriptSelector_ || !debugHandlerSelector_ || handlerName.trimmed().isEmpty()) {
        return;
    }

    const QString target = handlerName.trimmed();
    int targetScriptIndex = -1;
    int targetHandlerIndex = -1;
    for (int scriptIndex = 0; scriptIndex < debugScriptHandlerPreviews_.size(); ++scriptIndex) {
        const auto& previews = debugScriptHandlerPreviews_.at(scriptIndex);
        for (int handlerIndex = 0; handlerIndex < previews.size(); ++handlerIndex) {
            QString previewHandlerName = previews.at(handlerIndex).section(QLatin1Char('\n'), 0, 0).trimmed();
            if (previewHandlerName.startsWith(QStringLiteral("on "))) {
                previewHandlerName = previewHandlerName.mid(3).trimmed();
            }
            if (previewHandlerName.compare(target, Qt::CaseInsensitive) == 0) {
                targetScriptIndex = scriptIndex;
                targetHandlerIndex = handlerIndex;
                break;
            }
        }
        if (targetScriptIndex >= 0) {
            break;
        }
    }
    if (targetScriptIndex < 0 || targetHandlerIndex < 0) {
        return;
    }

    if (debugScriptFilter_ && !debugScriptFilter_->text().isEmpty()) {
        debugScriptFilter_->clear();
    }
    if (debugHandlerFilter_ && !debugHandlerFilter_->text().isEmpty()) {
        debugHandlerFilter_->clear();
    }
    refreshBytecodeDebuggerScriptFilter();

    const int scriptId = targetScriptIndex < debugScriptIds_.size() ? debugScriptIds_.at(targetScriptIndex) : -1;
    int selectorIndex = -1;
    for (int index = 0; index < debugScriptSelector_->count(); ++index) {
        bool ok = false;
        const int itemScriptId = debugScriptSelector_->itemData(index, Qt::UserRole + 2).toInt(&ok);
        if (ok && itemScriptId == scriptId) {
            selectorIndex = index;
            break;
        }
    }
    if (selectorIndex >= 0) {
        debugScriptSelector_->setCurrentIndex(selectorIndex);
    }

    const int handlerSelectorIndex = targetHandlerIndex + 1;
    if (handlerSelectorIndex >= 0 && handlerSelectorIndex < debugHandlerSelector_->count()) {
        debugHandlerSelector_->setCurrentIndex(handlerSelectorIndex);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(debugNavigatedToHandlerStatusText(target.toStdString())));
    }
    if (showDetails) {
        showBytecodeHandlerDetails();
    }
}

int MainWindow::currentDebugScriptId() const {
    if (!debugScriptSelector_) {
        return -1;
    }
    bool ok = false;
    const int scriptId = debugScriptSelector_->currentData(Qt::UserRole + 2).toInt(&ok);
    return ok ? scriptId : -1;
}

QString MainWindow::currentDebugHandlerName() const {
    if (!debugHandlerSelector_ || debugHandlerSelector_->currentIndex() <= 0) {
        return {};
    }
    QString handlerName = debugHandlerSelector_->currentText().trimmed();
    if (handlerName.startsWith(QStringLiteral("on "))) {
        handlerName = handlerName.mid(3).trimmed();
    }
    return handlerName;
}

std::optional<int> MainWindow::selectedDebugInstructionOffset() const {
    if (!debugBytecodeList_ || !debugBytecodeList_->currentItem()) {
        return std::nullopt;
    }
    const QVariant offset = debugBytecodeList_->currentItem()->data(kBytecodeOffsetRole);
    if (!offset.isValid()) {
        return std::nullopt;
    }
    bool ok = false;
    const int value = offset.toInt(&ok);
    return ok ? std::optional<int>{value} : std::nullopt;
}

QString MainWindow::selectedDebugCallTargetName() const {
    if (!debugBytecodeList_ || !debugBytecodeList_->currentItem()) {
        return {};
    }
    return debugBytecodeList_->currentItem()->data(kBytecodeCallTargetRole).toString();
}

QString MainWindow::decorateDebugHandlerPreview(const QString& preview, int scriptId, const QString& handlerName) const {
    if (!debugController_ || scriptId < 0) {
        return preview;
    }
    QString normalizedHandlerName = handlerName.trimmed();
    if (normalizedHandlerName.startsWith(QStringLiteral("on "))) {
        normalizedHandlerName = normalizedHandlerName.mid(3).trimmed();
    }

    QStringList lines = preview.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        const int bracket = line.indexOf(QLatin1Char('['));
        const int close = line.indexOf(QLatin1Char(']'), bracket + 1);
        if (bracket < 0 || close <= bracket + 1) {
            continue;
        }
        bool ok = false;
        const int offset = line.mid(bracket + 1, close - bracket - 1).trimmed().toInt(&ok);
        if (!ok) {
            continue;
        }
        if (line.startsWith(QStringLiteral("B "))) {
            line.remove(0, 2);
        }
        if (debugController_->hasBreakpoint(scriptId, normalizedHandlerName.toStdString(), offset)) {
            line.prepend(QStringLiteral("B "));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

bool MainWindow::debugHandlerExists(const QString& handlerName) const {
    const QString target = handlerName.trimmed();
    if (target.isEmpty()) {
        return false;
    }
    for (const auto& previews : debugScriptHandlerPreviews_) {
        for (const auto& preview : previews) {
            QString previewHandlerName = preview.section(QLatin1Char('\n'), 0, 0).trimmed();
            if (previewHandlerName.startsWith(QStringLiteral("on "))) {
                previewHandlerName = previewHandlerName.mid(3).trimmed();
            }
            if (previewHandlerName.compare(target, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::showPanel(const QString& panelId) {
    auto found = panels_.find(panelId);
    if (found == panels_.end()) {
        return;
    }
    found.value()->setVisible(true);
    found.value()->raise();
    if (auto action = panelActions_.value(panelId, nullptr)) {
        action->setChecked(true);
    }
}

bool MainWindow::selectScriptEditorScriptId(int scriptId) {
    if (scriptId <= 0 || !scriptSelector_) {
        return false;
    }

    int selectorIndex = -1;
    for (int index = 0; index < scriptSelector_->count(); ++index) {
        bool ok = false;
        const int itemScriptId = scriptSelector_->itemData(index, Qt::UserRole + 1).toInt(&ok);
        if (ok && itemScriptId == scriptId) {
            selectorIndex = index;
            break;
        }
    }
    if (selectorIndex < 0) {
        return false;
    }

    scriptSelector_->setCurrentIndex(selectorIndex);
    updateScriptEditorPreview();

    if (scriptTable_) {
        const bool blocked = scriptTable_->blockSignals(true);
        scriptTable_->clearSelection();
        for (int row = 0; row < scriptTable_->rowCount(); ++row) {
            const auto* idItem = scriptTable_->item(row, 0);
            if (!idItem) {
                continue;
            }
            bool ok = false;
            const int rowScriptId = idItem->data(Qt::UserRole + 1).toInt(&ok);
            if (ok && rowScriptId == scriptId) {
                scriptTable_->selectRow(row);
                scriptTable_->scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
        scriptTable_->blockSignals(blocked);
    }
    return true;
}

void MainWindow::updatePropertyInspectorForSelection(const SelectionState& selection) {
    if (selectionSummary_) {
        selectionSummary_->setText(toQString(selectionDetails(selection)));
    }
    clearPropertyGridValues(propertySpriteFields_);
    if (behaviorList_) {
        behaviorList_->clear();
        behaviorList_->addItem(toQString(propertyInspectorBehaviorPlaceholderText()));
    }

    const auto movie = context_.movie();
    if (movie) {
        if (const auto sprite = findScoreIntervalForSelection(*movie, selection)) {
            const auto values = propertyInspectorSpriteValues(*sprite);
            setPropertyGridValues(propertySpriteFields_, values);
            if (stagePlayer_ && selection.frame == stagePlayer_->currentFrame()) {
                if (auto liveSprite = stagePlayer_->stageRenderer().spriteRegistry().get(selection.channel)) {
                    setPropertyGridFieldValue(propertySpriteFields_, 2, QString::number(liveSprite->locH()));
                    setPropertyGridFieldValue(propertySpriteFields_, 3, QString::number(liveSprite->locV()));
                    setPropertyGridFieldValue(propertySpriteFields_, 4, QString::number(liveSprite->width()));
                    setPropertyGridFieldValue(propertySpriteFields_, 5, QString::number(liveSprite->height()));
                    setPropertyGridFieldValue(propertySpriteFields_, 6, QString::number(liveSprite->ink()));
                    setPropertyGridFieldValue(propertySpriteFields_, 7, QString::number(liveSprite->blend()));
                    setPropertyGridFieldValue(propertySpriteFields_, 8, QString::number(liveSprite->locZ()));
                    setPropertyGridFieldValue(propertySpriteFields_, 9, boolText(liveSprite->isVisible()));
                    setPropertyGridFieldValue(propertySpriteFields_,
                                              10,
                                              datumBoolText(stagePlayer_->spriteProperties().getSpriteProp(
                                                  selection.channel,
                                                  "moveableSprite")));
                    setPropertyGridFieldValue(propertySpriteFields_,
                                              11,
                                              datumBoolText(stagePlayer_->spriteProperties().getSpriteProp(
                                                  selection.channel,
                                                  "editableText")));
                }
            }
            const auto behaviors = propertyInspectorBehaviorValues(*sprite);
            setListValues(behaviorList_, behaviors);
            if (behaviorList_ && sprite->scriptId > 0) {
                for (int row = 0; row < behaviorList_->count(); ++row) {
                    if (auto* behavior = behaviorList_->item(row)) {
                        behavior->setData(kBehaviorScriptIdRole, sprite->scriptId);
                        behavior->setToolTip(toQString(propertyInspectorBehaviorScriptTooltipText(sprite->scriptId)));
                    }
                }
            }
            if (behaviorList_ && stagePlayer_ && selection.frame == stagePlayer_->currentFrame()) {
                const auto runtimeInstances = stagePlayer_->behaviorManager().getInstancesForChannel(selection.channel);
                if (!runtimeInstances.empty()) {
                    std::vector<std::string> runtimeBehaviors;
                    runtimeBehaviors.reserve(runtimeInstances.size());
                    std::vector<int> scriptIds;
                    scriptIds.reserve(runtimeInstances.size());
                    for (const auto& instance : runtimeInstances) {
                        const int scriptId = instance && instance->script() ? instance->script()->id().value() : 0;
                        scriptIds.push_back(scriptId);
                        runtimeBehaviors.push_back(propertyInspectorRuntimeBehaviorValue(
                            instance ? instance->id() : 0,
                            scriptId,
                            instance ? instance->behaviorRef().toString() : std::string{},
                            instance ? static_cast<int>(instance->properties().size()) : 0));
                    }
                    setListValues(behaviorList_, runtimeBehaviors);
                    for (int row = 0; row < behaviorList_->count() && row < static_cast<int>(scriptIds.size()); ++row) {
                        if (auto* behavior = behaviorList_->item(row)) {
                            if (scriptIds[static_cast<std::size_t>(row)] > 0) {
                                behavior->setData(kBehaviorScriptIdRole, scriptIds[static_cast<std::size_t>(row)]);
                                behavior->setToolTip(toQString(propertyInspectorRuntimeBehaviorScriptTooltipText(
                                    scriptIds[static_cast<std::size_t>(row)])));
                            }
                        }
                    }
                }
            }
        }
    }

    if (behaviorList_ &&
        (selection.type == SelectionType::Sprite || selection.type == SelectionType::ScoreCell) &&
        selection.channel > 0) {
        const auto firstText = behaviorList_->count() > 0 ? behaviorList_->item(0)->text() : QString{};
        bool appendedPending = false;
        for (const auto& change : pendingBehaviorChanges_) {
            if (change.channel != selection.channel || change.frame != selection.frame) {
                continue;
            }
            if (!appendedPending &&
                (firstText == toQString(propertyInspectorBehaviorPlaceholderText()) ||
                 firstText == QStringLiteral("(No behaviors attached)"))) {
                behaviorList_->clear();
            }
            appendedPending = true;
            const QString text = change.removal
                ? toQString(propertyInspectorPendingBehaviorRemovalValue(change.behaviorName.toStdString()))
                : toQString(propertyInspectorPendingBehaviorValue(change.scriptId));
            auto* item = new QListWidgetItem(text, behaviorList_);
            item->setData(kBehaviorScriptIdRole, change.scriptId);
            item->setData(kBehaviorPendingActionRole, change.removal ? 2 : 1);
            item->setData(kBehaviorPendingChannelRole, change.channel);
            item->setData(kBehaviorPendingFrameRole, change.frame);
            if (!change.removal && change.scriptId > 0) {
                item->setToolTip(toQString(propertyInspectorBehaviorScriptTooltipText(change.scriptId)));
            }
        }
    }

    if (selection.type != SelectionType::CastMember) {
        if (selectedMemberDetails_) {
            selectedMemberDetails_->setText(toQString(propertyInspectorText().memberPlaceholderText));
        }
        clearPropertyGridValues(propertyMemberFields_);
        return;
    }

    if (selectedMemberDetails_) {
        selectedMemberDetails_->setText(toQString(propertyInspectorCastMemberHeading(selection.castLib, selection.memberNum)));
    }

    if (!movie) {
        clearPropertyGridValues(propertyMemberFields_);
        return;
    }

    std::vector<CastMemberRow> memberRows;
    QString castName = toQString(castWindowDefaultCastName());
    if (selection.castLib > 0 && stagePlayer_) {
        if (auto castLib = stagePlayer_->castLibManager().getCastLib(selection.castLib);
            castLib && castLib->isLoaded()) {
            memberRows = buildRuntimeCastMemberRows(*castLib, {});
            castName = toQString(castLib->name());
            if (castName.trimmed().isEmpty()) {
                castName = toQString(castWindowCastLibraryLabel(selection.castLib, {}, false, castLib->isExternal(), castLib->isLoaded()));
            }
        }
    } else {
        memberRows = buildCastMemberRows(*movie);
    }
    for (const auto& row : memberRows) {
        if (row.chunkId == selection.memberNum) {
            const auto values = propertyInspectorMemberValues(row, castName.toStdString());
            setPropertyGridValues(propertyMemberFields_, values);
            return;
        }
    }
    clearPropertyGridValues(propertyMemberFields_);
}

void MainWindow::openSelectedBehaviorScript() {
    const auto inspectorText = propertyInspectorText();
    if (!behaviorList_) {
        return;
    }
    auto* current = behaviorList_->currentItem();
    if (!current) {
        return;
    }
    bool ok = false;
    const int scriptId = current->data(kBehaviorScriptIdRole).toInt(&ok);
    if (!ok || scriptId <= 0) {
        return;
    }

    showPanel(QStringLiteral("script"));
    if (!selectScriptEditorScriptId(scriptId)) {
        QMessageBox::information(this,
                                 toQString(inspectorText.behaviorScriptTitle),
                                 toQString(propertyInspectorMissingBehaviorScriptText(scriptId)));
    }
}

void MainWindow::prepareAddBehaviorToSelectedSprite() {
    const auto inspectorText = propertyInspectorText();
    const auto selection = context_.selection();
    if ((selection.type != SelectionType::Sprite && selection.type != SelectionType::ScoreCell) || selection.channel <= 0) {
        QMessageBox::information(this,
                                 toQString(inspectorText.addBehaviorTitle),
                                 toQString(inspectorText.addBehaviorNoSpriteText));
        showPanel(QStringLiteral("score"));
        return;
    }

    bool ok = false;
    const int scriptId = QInputDialog::getInt(this,
                                             toQString(inspectorText.addBehaviorTitle),
                                             toQString(propertyInspectorAddBehaviorPromptText(selection.channel,
                                                                                            selection.frame)),
                                             1,
                                             1,
                                             999999,
                                             1,
                                             &ok);
    if (!ok) {
        return;
    }

    const auto duplicate = std::find_if(pendingBehaviorChanges_.cbegin(),
                                        pendingBehaviorChanges_.cend(),
                                        [selection, scriptId](const PendingBehaviorChange& change) {
                                            return !change.removal &&
                                                   change.channel == selection.channel &&
                                                   change.frame == selection.frame &&
                                                   change.scriptId == scriptId;
                                        });
    if (duplicate == pendingBehaviorChanges_.cend()) {
        pendingBehaviorChanges_.push_back(PendingBehaviorChange{
            .channel = selection.channel,
            .frame = selection.frame,
            .scriptId = scriptId,
        });
    }
    showPanel(QStringLiteral("property-inspector"));
    updatePropertyInspectorForSelection(selection);
    QMessageBox::information(this,
                             toQString(inspectorText.addBehaviorTitle),
                             toQString(propertyInspectorPreparedAddBehaviorText(scriptId,
                                                                               selection.channel,
                                                                               selection.frame)));
    statusLabel_->setText(toQString(propertyInspectorPreparedAddBehaviorStatusText(scriptId, selection.channel)));
}

void MainWindow::prepareRemoveSelectedBehavior() {
    const auto inspectorText = propertyInspectorText();
    if (!behaviorList_) {
        return;
    }
    auto* current = behaviorList_->currentItem();
    if (!current || current->text().startsWith(QLatin1Char('('))) {
        QMessageBox::information(this,
                                 toQString(inspectorText.removeBehaviorTitle),
                                 toQString(inspectorText.removeBehaviorNoSelectionText));
        return;
    }

    const auto selection = context_.selection();
    bool ok = false;
    const int pendingAction = current->data(kBehaviorPendingActionRole).toInt(&ok);
    if (ok && pendingAction == 1) {
        const int scriptId = current->data(kBehaviorScriptIdRole).toInt();
        pendingBehaviorChanges_.erase(std::remove_if(pendingBehaviorChanges_.begin(),
                                                     pendingBehaviorChanges_.end(),
                                                     [selection, scriptId](const PendingBehaviorChange& change) {
                                                         return !change.removal &&
                                                                change.channel == selection.channel &&
                                                                change.frame == selection.frame &&
                                                                change.scriptId == scriptId;
                                                     }),
                                      pendingBehaviorChanges_.end());
        updatePropertyInspectorForSelection(selection);
        if (statusLabel_) {
            statusLabel_->setText(toQString(propertyInspectorCanceledPendingBehaviorStatusText(selection.channel)));
        }
        return;
    }

    const QString behaviorName = current->text();
    const auto duplicate = std::find_if(pendingBehaviorChanges_.cbegin(),
                                        pendingBehaviorChanges_.cend(),
                                        [selection, &behaviorName](const PendingBehaviorChange& change) {
                                            return change.removal &&
                                                   change.channel == selection.channel &&
                                                   change.frame == selection.frame &&
                                                   change.behaviorName == behaviorName;
                                        });
    if (duplicate == pendingBehaviorChanges_.cend()) {
        pendingBehaviorChanges_.push_back(PendingBehaviorChange{
            .channel = selection.channel,
            .frame = selection.frame,
            .scriptId = current->data(kBehaviorScriptIdRole).toInt(),
            .behaviorName = behaviorName,
            .removal = true,
        });
    }
    updatePropertyInspectorForSelection(selection);
    QMessageBox::information(this,
                             toQString(inspectorText.removeBehaviorTitle),
                             toQString(propertyInspectorPreparedRemoveBehaviorText(behaviorName.toStdString(),
                                                                                  selection.channel,
                                                                                  selection.frame)));
    statusLabel_->setText(toQString(propertyInspectorPreparedRemoveBehaviorStatusText(selection.channel)));
}

void MainWindow::loadSelectedExternalCast() {
    const auto castText = castWindowText();
    if (!stagePlayer_ || !castSelector_) {
        QMessageBox::warning(this, toQString(castText.loadExternalCastTitle), toQString(castText.loadExternalCastNoMovieText));
        return;
    }

    const int castLib = castSelector_->currentData().toInt();
    if (castLib <= 0) {
        QMessageBox::information(this,
                                 toQString(castText.loadExternalCastTitle),
                                 toQString(castText.loadExternalCastSelectSlotText));
        return;
    }

    auto castLibrary = stagePlayer_->castLibManager().getCastLib(castLib);
    if (!castLibrary || !castLibrary->isExternal()) {
        QMessageBox::information(this,
                                 toQString(castText.loadExternalCastTitle),
                                 toQString(castText.loadExternalCastNotExternalText));
        return;
    }

    const QString authoredName = toQString(castLibrary->fileName());
    const QString directory = context_.currentPath().empty()
                                  ? EditorPreferences::lastOpenDirectory()
                                  : QString::fromStdString(context_.currentPath().parent_path().string());
    const QString path = QFileDialog::getOpenFileName(
        this,
        toQString(castWindowLoadExternalCastDialogTitle(authoredName.toStdString())),
        directory,
        toQString(castText.loadExternalCastFileFilter));
    if (path.isEmpty()) {
        return;
    }

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this,
                              toQString(castText.loadExternalCastTitle),
                              toQString(castText.loadExternalCastOpenFailedText));
        return;
    }
    const QByteArray bytes = input.readAll();
    std::vector<std::uint8_t> data;
    data.reserve(static_cast<std::size_t>(bytes.size()));
    for (const auto byte : bytes) {
        data.push_back(static_cast<std::uint8_t>(byte));
    }
    if (!stagePlayer_->loadExternalCastFromCachedData(castLib, data)) {
        QMessageBox::critical(this,
                              toQString(castText.loadExternalCastTitle),
                              toQString(castText.loadExternalCastLoadFailedText));
        return;
    }

    EditorPreferences::setLastOpenDirectory(QFileInfo(path).absolutePath());
    castThumbnailCache_.clear();
    updateMovieViews();
    refreshStageFrame();
    if (statusLabel_) {
        statusLabel_->setText(toQString(castWindowLoadedExternalCastStatus(castLib)));
    }
}

void MainWindow::applyTextEditorChanges() {
    const auto textEditorStrings = textEditorText();
    if (!stagePlayer_ || !textEditorText_ || textEditorMemberNum_ <= 0) {
        if (textEditorStatus_) {
            textEditorStatus_->setText(toQString(textEditorStrings.noMemberLoadedStatus));
        }
        return;
    }
    const int castLib = textEditorCastLib_ > 0 ? textEditorCastLib_ : 1;
    const auto text = textEditorText_->toPlainText().toStdString();
    const bool applied = stagePlayer_->castLibManager().setMemberProp(
        castLib,
        textEditorMemberNum_,
        "text",
        lingo::Datum::of(text));
    if (textEditorStatus_) {
        textEditorStatus_->setText(toQString(applied ? textEditorStrings.appliedRuntimeStatus
                                                     : textEditorStrings.applyFailedStatus));
    }
    if (applied) {
        textEditorText_->document()->setModified(false);
        refreshStageFrame();
    }
}

void MainWindow::applyFieldEditorChanges() {
    const auto fieldEditorStrings = fieldEditorText();
    if (!stagePlayer_ || !fieldText_ || fieldEditorMemberNum_ <= 0) {
        if (fieldStatus_) {
            fieldStatus_->setText(toQString(fieldEditorStrings.noMemberLoadedStatus));
        }
        return;
    }
    const int castLib = fieldEditorCastLib_ > 0 ? fieldEditorCastLib_ : 1;
    const auto text = fieldText_->toPlainText().toStdString();
    const bool applied = stagePlayer_->castLibManager().setMemberProp(
        castLib,
        fieldEditorMemberNum_,
        "text",
        lingo::Datum::of(text));
    if (fieldStatus_) {
        fieldStatus_->setText(toQString(applied ? fieldEditorStrings.appliedRuntimeStatus
                                                : fieldEditorStrings.applyFailedStatus));
    }
    if (applied) {
        fieldText_->document()->setModified(false);
        refreshStageFrame();
    }
}

std::shared_ptr<DirectorFile> MainWindow::sourceFileForCastLib(int castLib) {
    if (castLib <= 0) {
        return context_.movie();
    }
    if (!stagePlayer_) {
        return nullptr;
    }
    auto castLibrary = stagePlayer_->castLibManager().getCastLib(castLib);
    if (!castLibrary || !castLibrary->isLoaded()) {
        return nullptr;
    }
    return castLibrary->sourceFile();
}

std::shared_ptr<chunks::CastMemberChunk> MainWindow::castMemberForCastLib(int castLib, int memberNum) {
    if (memberNum <= 0) {
        return nullptr;
    }
    if (castLib <= 0) {
        const auto movie = context_.movie();
        return movie ? movie->getCastMemberByNumber(0, memberNum) : nullptr;
    }
    if (!stagePlayer_) {
        return nullptr;
    }
    auto castLibrary = stagePlayer_->castLibManager().getCastLib(castLib);
    return castLibrary && castLibrary->isLoaded() ? castLibrary->findMemberByNumber(memberNum) : nullptr;
}

QList<MainWindow::CastMemberRef> MainWindow::selectedCastMemberRefs() const {
    QList<CastMemberRef> refs;
    auto appendRef = [&refs](int castLib, int memberNum) {
        if (memberNum <= 0) {
            return;
        }
        const auto duplicate = std::find_if(refs.cbegin(), refs.cend(), [castLib, memberNum](const CastMemberRef& ref) {
            return ref.castLib == castLib && ref.memberNum == memberNum;
        });
        if (duplicate == refs.cend()) {
            refs.push_back(CastMemberRef{.castLib = castLib, .memberNum = memberNum});
        }
    };
    if (castGrid_ && castGrid_->isVisible()) {
        for (const auto* item : castGrid_->selectedItems()) {
            const int memberNum = item ? item->data(kCastMemberNumberRole).toInt() : 0;
            const int castLib = item ? item->data(kCastLibraryNumberRole).toInt() : 0;
            appendRef(castLib, memberNum);
        }
        if (!refs.isEmpty()) {
            return refs;
        }
    }
    if (castTable_ && castTable_->selectionModel()) {
        const auto rows = castTable_->selectionModel()->selectedRows();
        for (const auto& row : rows) {
            const auto* item = castTable_->item(row.row(), 0);
            const int memberNum = item ? item->data(kCastMemberNumberRole).toInt() : 0;
            const int castLib = item ? item->data(kCastLibraryNumberRole).toInt() : 0;
            appendRef(castLib, memberNum);
        }
    }
    if (refs.isEmpty()) {
        const auto selection = context_.selection();
        if (selection.type == SelectionType::CastMember) {
            appendRef(selection.castLib, selection.memberNum);
        }
    }
    return refs;
}

void MainWindow::exportSelectedCastMembers() {
    const auto castText = castWindowText();
    const auto memberRefs = selectedCastMemberRefs();
    if (memberRefs.isEmpty()) {
        QMessageBox::information(this,
                                 toQString(castText.exportMembersTitle),
                                 toQString(castText.exportMembersNoSelectionText));
        showPanel(QStringLiteral("cast"));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, toQString(castText.exportSelectedDirectoryTitle));
    if (directory.isEmpty()) {
        return;
    }
    if (castStatusLabel_) {
        castStatusLabel_->setText(toQString(castWindowExportStartingStatus(memberRefs.size())));
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(castWindowExportStartingStatus(memberRefs.size())).trimmed());
    }
    const int exported = exportCastMembersToDirectory(memberRefs, directory);
    const QString status = toQString(exported > 0 ? castWindowExportedStatus(exported)
                                                 : castWindowExportFailedStatus(memberRefs.size()));
    if (castStatusLabel_) {
        castStatusLabel_->setText(status);
    }
    if (statusLabel_) {
        statusLabel_->setText(status.trimmed());
    }
}

void MainWindow::openSelectedCastMember() {
    if (castGrid_ && castGrid_->isVisible()) {
        auto* item = castGrid_->currentItem();
        if (!item && !castGrid_->selectedItems().isEmpty()) {
            item = castGrid_->selectedItems().first();
        }
        const QString details = item ? item->data(kCastDetailsRole).toString() : QString();
        const QString targetPanel = item ? item->data(kCastTargetPanelRole).toString() : QString();
        const int castLib = item ? item->data(kCastLibraryNumberRole).toInt() : 0;
        const int memberNum = item ? item->data(kCastMemberNumberRole).toInt() : 0;
        if (!targetPanel.isEmpty()) {
            loadCastMemberIntoPanel(targetPanel, details, castLib, memberNum);
        }
        return;
    }
    if (!castTable_) {
        return;
    }
    const auto selected = castTable_->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const int row = selected.first()->row();
    const auto* firstItem = castTable_->item(row, 0);
    const QString details = firstItem ? firstItem->data(kCastDetailsRole).toString() : QString();
    const QString targetPanel = firstItem ? firstItem->data(kCastTargetPanelRole).toString() : QString();
    const int castLib = firstItem ? firstItem->data(kCastLibraryNumberRole).toInt() : 0;
    const int memberNum = firstItem ? firstItem->data(kCastMemberNumberRole).toInt() : 0;
    if (!targetPanel.isEmpty()) {
        loadCastMemberIntoPanel(targetPanel, details, castLib, memberNum);
    }
}

bool MainWindow::exportCastMemberToFile(int castLib, int memberNum, const QString& path) {
    const auto sourceFile = sourceFileForCastLib(castLib);
    if (!sourceFile || memberNum <= 0 || path.isEmpty()) {
        return false;
    }
    const auto member = castMemberForCastLib(castLib, memberNum);
    if (!member) {
        return false;
    }

    if (member->memberType() == cast::MemberType::Bitmap || member->memberType() == cast::MemberType::Picture) {
        const auto decoded = sourceFile->decodeBitmap(member);
        if (!decoded) {
            return false;
        }
        const auto image = bitmapToImage(*decoded);
        return !image.isNull() && image.save(path, "PNG");
    }

    if (member->memberType() == cast::MemberType::Sound) {
        const auto sound = player::audio::SoundManager::findSoundForMember(*sourceFile, member);
        if (!sound) {
            return false;
        }
        const auto payload = soundExportPayload(*sound);
        if (!payload || payload->bytes.empty()) {
            return false;
        }
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly)) {
            return false;
        }
        const auto written = output.write(reinterpret_cast<const char*>(payload->bytes.data()),
                                          static_cast<qint64>(payload->bytes.size()));
        return written == static_cast<qint64>(payload->bytes.size());
    }

    if (member->memberType() == cast::MemberType::Palette) {
        const auto palette = sourceFile->resolvePaletteByMemberNumber(memberNum);
        if (!palette) {
            return false;
        }
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        const auto text = toQString(paletteExportText(palette->colors())).toUtf8();
        const auto written = output.write(text);
        return written == text.size();
    }

    if (member->memberType() == cast::MemberType::Text ||
        member->memberType() == cast::MemberType::RichText ||
        member->memberType() == cast::MemberType::Button) {
        const auto textChunk = sourceFile->getTextForMember(member);
        if (!textChunk) {
            return false;
        }
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        const auto text = toQString(textExportText(*textChunk)).toUtf8();
        const auto written = output.write(text);
        return written == text.size();
    }

    if (member->memberType() == cast::MemberType::Script) {
        const auto script = sourceFile->getScriptForCastMember(member);
        const auto names = script ? sourceFile->getScriptNamesForScript(script) : nullptr;
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        const auto text = toQString(scriptExportText(member->name(), memberNum, script.get(), names.get())).toUtf8();
        const auto written = output.write(text);
        return written == text.size();
    }

    if (member->specificData().empty()) {
        return false;
    }

    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    const auto& bytes = member->specificData();
    const auto written = output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    return written == static_cast<qint64>(bytes.size());
}

int MainWindow::exportCastMembersToDirectory(const QList<CastMemberRef>& memberRefs, const QString& directory) {
    if (memberRefs.isEmpty() || directory.isEmpty()) {
        return 0;
    }

    QDir outputDir(directory);
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        return 0;
    }

    int exported = 0;
    for (const auto& ref : memberRefs) {
        const auto sourceFile = sourceFileForCastLib(ref.castLib);
        const auto member = castMemberForCastLib(ref.castLib, ref.memberNum);
        if (!sourceFile || !member) {
            continue;
        }
        const auto row = castMemberRowFromChunk(ref.memberNum, *member);
        QString fileName = castMemberExportFileNameForMovie(*sourceFile, row, member);
        QString path = outputDir.filePath(fileName);
        for (int suffix = 1; QFileInfo::exists(path); ++suffix) {
            const auto extension = QFileInfo(fileName).suffix();
            fileName = toQString(castMemberExportDuplicateFileName(row, suffix, extension.toStdString()));
            path = outputDir.filePath(fileName);
        }
        if (exportCastMemberToFile(ref.castLib, ref.memberNum, path)) {
            ++exported;
        }
    }
    return exported;
}

void MainWindow::showDetailedStackWindow(bool visible) {
    auto* dialog = detailedStackWindow_ ? detailedStackWindow_.get() : createDetailedStackWindow();
    if (!dialog) {
        return;
    }
    if (visible) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    } else {
        dialog->hide();
    }
}

void MainWindow::updateDetailedStackFromSnapshot(const ::libreshockwave::player::debug::DebugSnapshot& snapshot) {
    showPanel(QStringLiteral("bytecode-debugger"));
    if (debugStatusLabel_) {
        debugStatusLabel_->setText(toQString(debugPausedStatusText(snapshot)));
    }
    if (debugHandlerLabel_) {
        debugHandlerLabel_->setText(toQString(debugPausedHandlerText(snapshot)));
    }
    setDebugTableRows(debugStackTable_, debugStackRows(snapshot.stack));
    setDebugTableRows(debugLocalsTable_, debugVariableRows(snapshot.locals));
    setDebugTableRows(debugGlobalsTable_, debugVariableRows(snapshot.globals));
    setDebugTableRows(debugWatchesTable_, debugWatchRows(snapshot.watchResults));
    refreshDebugObjectsTables();
    if (debugBytecodeList_ && !snapshot.allInstructions.empty()) {
        setDebugBytecodeListing(toQString(debugInstructionListingText(snapshot)));
        selectDebugInstructionOffset(snapshot.instructionOffset);
    }
    if (!detailedStackWindow_) {
        (void)createDetailedStackWindow();
    }
    if (detailedStackStatus_) {
        detailedStackStatus_->setText(toQString(detailedStackPausedStatus(snapshot)));
    }
    if (detailedStackCallStackText_) {
        detailedStackCallStackText_->setPlainText(toQString(detailedStackCallStackText(snapshot.callStack)));
    }
    if (detailedStackVmStackText_) {
        detailedStackVmStackText_->setPlainText(toQString(detailedStackVmStackText(snapshot.stack)));
        detailedStackVmStackText_->moveCursor(QTextCursor::End);
    }
    if (detailedStackArgumentsText_) {
        detailedStackArgumentsText_->setPlainText(toQString(detailedStackArgumentsText(snapshot.arguments)));
    }
    if (detailedStackReceiverText_) {
        detailedStackReceiverText_->setPlainText(toQString(detailedStackReceiverText(snapshot.receiver)));
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(detailedStackPausedStatus(snapshot)));
    }
    if (detailedStackWindow_ && detailedStackWindow_->isVisible()) {
        detailedStackWindow_->raise();
        detailedStackWindow_->activateWindow();
    }
}

void MainWindow::markDetailedStackRunning() {
    if (debugStatusLabel_) {
        debugStatusLabel_->setText(toQString(debugInitialStatusText()));
    }
    if (debugHandlerLabel_) {
        debugHandlerLabel_->setText(toQString(debugInitialHandlerText()));
    }
    setDebugTableRows(debugStackTable_, {});
    setDebugTableRows(debugLocalsTable_, {});
    setDebugTableRows(debugGlobalsTable_, {});
    setDebugTableRows(debugWatchesTable_, {});
    setDebugTableRows(debugTimeoutsTable_, {});
    setDebugTableRows(debugObjectGlobalsTable_, {});
    setDebugTableRows(debugMoviePropertiesTable_, {});
    if (detailedStackStatus_) {
        detailedStackStatus_->setText(toQString(detailedStackRunningStatusText()));
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(debugRunningStatusText()));
    }
}

void MainWindow::refreshDebugObjectsTables() {
    if (!stagePlayer_) {
        setDebugTableRows(debugTimeoutsTable_, {});
        setDebugTableRows(debugObjectGlobalsTable_, {});
        setDebugTableRows(debugMoviePropertiesTable_, {});
        return;
    }

    std::vector<::libreshockwave::player::timeout::TimeoutEntry> timeouts;
    for (const auto& name : stagePlayer_->timeoutManager().getTimeoutNames()) {
        if (const auto* entry = stagePlayer_->timeoutManager().getEntry(name)) {
            timeouts.push_back(*entry);
        }
    }
    setDebugTableRows(debugTimeoutsTable_, debugTimeoutRows(timeouts));

    std::map<std::string, lingo::Datum> globals;
    for (const auto& [name, value] : stagePlayer_->vm().globals()) {
        globals[name] = value;
    }
    setDebugTableRows(debugObjectGlobalsTable_, debugVariableRows(globals));

    std::vector<std::pair<std::string, lingo::Datum>> movieProperties;
    for (const auto propName : debugMoviePropertyNames()) {
        std::string prop{propName};
        movieProperties.emplace_back(prop, stagePlayer_->movieProperties().getMovieProp(prop));
    }
    setDebugTableRows(debugMoviePropertiesTable_, debugMoviePropertyRows(movieProperties));
}

void MainWindow::refreshDebugWatchTable() {
    if (!debugController_) {
        setDebugTableRows(debugWatchesTable_, {});
        return;
    }
    setDebugTableRows(debugWatchesTable_, debugWatchRows(debugController_->evaluateWatchExpressions()));
}

void MainWindow::showDebugDatumDetails(QTableWidget* table, int row, int column) {
    if (!table || row < 0 || column < 0) {
        return;
    }
    const auto* tableItem = table->item(row, column);
    if (!tableItem) {
        tableItem = table->item(row, 0);
    }
    if (!tableItem) {
        return;
    }

    const QString detailText = tableItem->data(Qt::UserRole + 3).toString();
    if (detailText.isEmpty()) {
        return;
    }
    const QString detailTitle = tableItem->data(Qt::UserRole + 1).toString();
    const QString detailType = tableItem->data(Qt::UserRole + 2).toString();

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(toQString(debugDatumDetailsTitle((detailTitle.isEmpty() ? tableItem->text() : detailTitle).toStdString())));
    dialog->resize(500, 400);

    auto* layout = new QVBoxLayout(dialog);
    auto* details = new QTextEdit(dialog);
    details->setReadOnly(true);
    details->setLineWrapMode(QTextEdit::WidgetWidth);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(12);
    details->setFont(monoFont);
    details->setPlainText(toQString(debugDatumDetailsText(detailType.toStdString(), detailText.toStdString())));
    layout->addWidget(details, 1);

    auto* closeButton = new QPushButton(toQString(debugDetailsText().closeButtonText), dialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    auto* buttonRow = new QWidget(dialog);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);
    layout->addWidget(buttonRow);

    dialog->show();
}

void MainWindow::addDebugWatch() {
    if (!debugController_) {
        return;
    }
    const auto watchText = debugWatchDialogText();
    bool accepted = false;
    const QString expression = QInputDialog::getText(this,
                                                     toQString(watchText.addTitle),
                                                     toQString(watchText.expressionPrompt),
                                                     QLineEdit::Normal,
                                                     QString{},
                                                     &accepted);
    if (!accepted || expression.trimmed().isEmpty()) {
        return;
    }
    const auto watch = debugController_->addWatchExpression(expression.trimmed().toStdString());
    (void)watch;
    refreshDebugWatchTable();
}

void MainWindow::editSelectedDebugWatch() {
    if (!debugController_ || !debugWatchesTable_) {
        return;
    }
    const auto selectedRows = debugWatchesTable_->selectionModel()
                                  ? debugWatchesTable_->selectionModel()->selectedRows()
                                  : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        return;
    }

    const auto* firstItem = debugWatchesTable_->item(selectedRows.front().row(), 0);
    if (!firstItem) {
        return;
    }
    const auto id = firstItem->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        return;
    }

    const auto watchText = debugWatchDialogText();
    bool accepted = false;
    const QString expression = QInputDialog::getText(this,
                                                     toQString(watchText.editTitle),
                                                     toQString(watchText.expressionPrompt),
                                                     QLineEdit::Normal,
                                                     firstItem->text(),
                                                     &accepted);
    if (!accepted || expression.trimmed().isEmpty()) {
        return;
    }
    const auto updatedWatch = debugController_->updateWatchExpression(id.toStdString(), expression.trimmed().toStdString());
    (void)updatedWatch;
    refreshDebugWatchTable();
}

void MainWindow::removeSelectedDebugWatch() {
    if (!debugController_ || !debugWatchesTable_) {
        return;
    }
    const auto selectedRows = debugWatchesTable_->selectionModel()
                                  ? debugWatchesTable_->selectionModel()->selectedRows()
                                  : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        return;
    }
    if (const auto* firstItem = debugWatchesTable_->item(selectedRows.front().row(), 0)) {
        const auto id = firstItem->data(Qt::UserRole).toString();
        if (!id.isEmpty()) {
            const bool removed = debugController_->removeWatchExpression(id.toStdString());
            (void)removed;
            refreshDebugWatchTable();
        }
    }
}

void MainWindow::clearDebugWatches() {
    if (!debugController_) {
        return;
    }
    debugController_->clearWatchExpressions();
    refreshDebugWatchTable();
}

void MainWindow::showTraceHandlerDialog() {
    const auto traceText = traceHandlerDialogText();
    if (!stagePlayer_) {
        QMessageBox::warning(this, toQString(traceText.title), toQString(traceText.noMovieText));
        return;
    }

    std::vector<std::string> currentHandlers;
    for (const auto& handler : stagePlayer_->vm().tracedHandlers()) {
        currentHandlers.push_back(handler);
    }
    const auto currentText = traceHandlerCurrentText(currentHandlers);

    bool accepted = false;
    const QString input = QInputDialog::getText(this,
                                                toQString(traceText.title),
                                                toQString(traceHandlerDialogPrompt(currentHandlers)),
                                                QLineEdit::Normal,
                                                currentText == toQString(traceText.noneText) ? QString{} : toQString(currentText),
                                                &accepted);
    if (!accepted) {
        return;
    }

    auto& vm = stagePlayer_->vm();
    vm.clearTraceHandlers();
    const auto handlers = traceHandlerNamesFromInput(input.toStdString());
    for (const auto& handler : handlers) {
        vm.addTraceHandler(handler);
    }
    if (statusLabel_) {
        statusLabel_->setText(toQString(traceHandlerStatusText(handlers.size())));
    }
}

void MainWindow::triggerDebugCommand(std::string_view commandId) {
    if (!debugController_) {
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugUnavailableStatusText()));
        }
        return;
    }

    if (commandId == "step-into") {
        debugController_->stepInto();
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugCommandRequestedStatusText(commandId)));
        }
    } else if (commandId == "step-over") {
        debugController_->stepOver();
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugCommandRequestedStatusText(commandId)));
        }
    } else if (commandId == "step-out") {
        debugController_->stepOut();
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugCommandRequestedStatusText(commandId)));
        }
    } else if (commandId == "continue") {
        debugController_->continueExecution();
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugCommandRequestedStatusText(commandId)));
        }
    } else if (commandId == "clear-breakpoints") {
        debugController_->clearAllBreakpoints();
        clearSavedBreakpoints();
        updateBytecodeDebuggerPreview();
        if (statusLabel_) {
            statusLabel_->setText(toQString(debugBreakpointsClearedStatusText()));
        }
    } else if (commandId == "toggle-breakpoint") {
        toggleSelectedBytecodeBreakpoint();
    }
}

QDialog* MainWindow::createDetailedStackWindow() {
    detailedStackWindow_ = std::make_unique<QDialog>(this);
    auto* dialog = detailedStackWindow_.get();
    dialog->setWindowTitle(toQString(detailedStackWindowTitle()));
    dialog->resize(500, 600);

    auto* layout = new QVBoxLayout(dialog);
    auto* status = new QLabel(toQString(detailedStackInitialStatusText()), dialog);
    detailedStackStatus_ = status;
    status->setStyleSheet(QStringLiteral("font-weight: 600;"));
    status->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(status);

    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(12);

    auto* tabs = new QTabWidget(dialog);
    auto addTextTab = [&](std::string_view title, const std::string& text, bool wrap) {
        auto* editor = new QPlainTextEdit(tabs);
        editor->setReadOnly(true);
        editor->setFont(monoFont);
        editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        editor->setPlainText(toQString(text));
        tabs->addTab(editor, toQString(title));
        return editor;
    };
    const auto names = detailedStackTabNames();
    detailedStackCallStackText_ = addTextTab(names[0], detailedStackCallStackPlaceholderText(), false);
    detailedStackVmStackText_ = addTextTab(names[1], detailedStackVmStackPlaceholderText(), false);
    detailedStackArgumentsText_ = addTextTab(names[2], detailedStackArgumentsPlaceholderText(), false);
    detailedStackReceiverText_ = addTextTab(names[3], detailedStackReceiverPlaceholderText(), true);
    layout->addWidget(tabs, 1);

    connect(dialog, &QDialog::finished, this, [this] {
        if (auto action = menuBar()->findChild<QAction*>(QStringLiteral("DetailedStackWindowAction"))) {
            action->setChecked(false);
        }
    });

    return dialog;
}

void MainWindow::loadCastMemberIntoPanel(const QString& panelId, const QString& details, int castLib, int memberNum) {
    if (auto* label = panelLoadLabels_.value(panelId, nullptr)) {
        label->setText(details);
    }
    const auto sourceFile = sourceFileForCastLib(castLib);
    const auto member = castMemberForCastLib(castLib, memberNum);
    const int runtimeCastLib = castLib > 0 ? castLib : 1;
    const auto runtimeMember = stagePlayer_ && memberNum > 0
                                   ? stagePlayer_->castLibManager().resolveMember(runtimeCastLib, memberNum)
                                   : nullptr;
    const bool hasLoadedMember = member != nullptr || runtimeMember != nullptr;
    const auto loadedMemberName = [&]() -> std::string {
        if (member) {
            return member->name();
        }
        if (runtimeMember) {
            return runtimeMember->name();
        }
        return {};
    };
    if (panelId == QStringLiteral("field") && fieldText_ && fieldStatus_) {
        const auto textChunk = sourceFile && member ? sourceFile->getTextForMember(member) : nullptr;
        if (textChunk) {
            fieldEditorCastLib_ = castLib;
            fieldEditorMemberNum_ = memberNum;
            const auto text = normalizedTextQString(textChunk->text());
            fieldText_->setPlainText(text);
            fieldText_->document()->setModified(false);
            fieldText_->moveCursor(QTextCursor::Start);
            fieldStatus_->setText(toQString(fieldEditorLoadedStatus(member->name(), memberNum, text.size())));
        } else if (runtimeMember && runtimeMember->isTextLike()) {
            fieldEditorCastLib_ = castLib;
            fieldEditorMemberNum_ = memberNum;
            const auto text = stagePlayer_->castLibManager().getMemberProp(runtimeCastLib, memberNum, "text").stringValue();
            const auto normalized = normalizedTextQString(text);
            fieldText_->setPlainText(normalized);
            fieldText_->document()->setModified(false);
            fieldText_->moveCursor(QTextCursor::Start);
            fieldStatus_->setText(toQString(fieldEditorLoadedStatus(loadedMemberName(), memberNum, normalized.size())));
        } else if (member) {
            fieldEditorCastLib_ = castLib;
            fieldEditorMemberNum_ = memberNum;
            fieldText_->setPlainText(toQString(fieldEditorNoDataText()));
            fieldText_->document()->setModified(false);
            fieldStatus_->setText(toQString(fieldEditorNoDataStatusText()));
        } else {
            fieldEditorCastLib_ = -1;
            fieldEditorMemberNum_ = -1;
            fieldText_->setPlainText(toQString(fieldEditorInitialText()));
            fieldText_->document()->setModified(false);
            fieldStatus_->setText(toQString(fieldEditorReadyStatus()));
        }
    }
    if (panelId == QStringLiteral("text") && textEditorText_ && textEditorStatus_) {
        const auto textChunk = sourceFile && member ? sourceFile->getTextForMember(member) : nullptr;
        if (member || (runtimeMember && runtimeMember->isTextLike())) {
            textEditorCastLib_ = castLib;
            textEditorMemberNum_ = memberNum;
            if (textChunk) {
                setTextEditorContent(textEditorText_, textChunk.get());
            } else {
                const auto text = runtimeMember && runtimeMember->isTextLike()
                                      ? stagePlayer_->castLibManager().getMemberProp(runtimeCastLib, memberNum, "text").stringValue()
                                      : std::string{};
                textEditorText_->setPlainText(normalizedTextQString(text));
            }
            textEditorText_->document()->setModified(false);
        } else {
            textEditorCastLib_ = -1;
            textEditorMemberNum_ = -1;
            textEditorText_->setPlainText(toQString(textEditorInitialText()));
            textEditorText_->document()->setModified(false);
        }
        textEditorStatus_->setText(hasLoadedMember ? toQString(textEditorLoadedStatus(loadedMemberName(), memberNum, textChunk != nullptr || (runtimeMember && runtimeMember->isTextLike())))
                                                   : toQString(textEditorReadyStatus()));
    }
    if (panelId == QStringLiteral("script") && scriptPreview_) {
        if (castLib > 0 && scriptCastSelector_) {
            const int index = scriptCastSelector_->findData(castLib);
            if (index >= 0 && scriptCastSelector_->currentIndex() != index) {
                scriptCastSelector_->setCurrentIndex(index);
            }
        }
        const auto script = sourceFile && member ? sourceFile->getScriptForCastMember(member) : nullptr;
        if (script && selectScriptEditorScriptId(script->id().value())) {
            if (scriptHandlerSelector_) {
                scriptHandlerSelector_->setCurrentIndex(0);
            }
        } else if (hasLoadedMember) {
            scriptPreview_->setPlainText(toQString(scriptEditorNoBytecodeForMemberText()));
        } else {
            scriptPreview_->setPlainText(toQString(scriptEditorInitialText()));
        }
    }
    if (panelId == QStringLiteral("paint") && paintImageLabel_ && paintStatus_) {
        paintImageLabel_->setPixmap({});
        paintOriginalPixmap_ = {};
        paintOriginalImage_ = {};
        paintEditorCastLib_ = -1;
        paintEditorMemberNum_ = -1;
        paintDragActive_ = false;
        paintDragTool_.clear();
        paintZoom_ = 1.0;
        auto decoded = sourceFile && member ? sourceFile->decodeBitmap(member) : std::nullopt;
        if (!decoded.has_value() && runtimeMember && runtimeMember->isBitmap()) {
            if (!runtimeMember->runtimeBitmap()) {
                (void)stagePlayer_->castLibManager().getMemberProp(runtimeCastLib, memberNum, "image");
            }
            if (const auto bitmap = runtimeMember->runtimeBitmap()) {
                decoded = *bitmap;
            }
        }
        if (decoded.has_value()) {
            const auto image = bitmapToImage(*decoded);
            if (!image.isNull()) {
                paintOriginalImage_ = image;
                paintOriginalPixmap_ = QPixmap::fromImage(image);
                paintEditorCastLib_ = castLib;
                paintEditorMemberNum_ = memberNum;
                paintBaseStatus_ = toQString(paintWindowLoadedStatus(loadedMemberName(),
                                                                     memberNum,
                                                                     decoded->width(),
                                                                     decoded->height(),
                                                                     decoded->bitDepth()));
                updatePaintPreview();
            } else {
                paintBaseStatus_.clear();
                paintImageLabel_->setText(toQString(paintWindowDecodeFailedText()));
                paintImageLabel_->setMinimumSize(320, 240);
                paintStatus_->setText(toQString(paintWindowErrorStatus()));
            }
        } else if (hasLoadedMember) {
            paintBaseStatus_.clear();
            paintImageLabel_->setText(toQString(paintWindowDecodeFailedText()));
            paintImageLabel_->setMinimumSize(320, 240);
            paintStatus_->setText(toQString(paintWindowErrorStatus()));
        } else {
            paintBaseStatus_.clear();
            paintImageLabel_->setText(details.isEmpty() ? toQString(paintWindowInitialText()) : details);
            paintImageLabel_->setMinimumSize(320, 240);
            paintStatus_->setText(details.isEmpty() ? toQString(paintWindowReadyStatus())
                                                    : toQString(routedMemberSelectedStatusText()));
        }
    }
    if (panelId == QStringLiteral("vector-shape") && vectorShapeCanvasLabel_ && vectorShapeStatus_) {
        vectorShapeCanvasLabel_->setPixmap({});
        vectorShapeCanvasLabel_->setMinimumSize(320, 240);
        vectorShapeLoaded_ = false;
        vectorShapeEditorCastLib_ = -1;
        vectorShapeEditorMemberNum_ = -1;
        if (vectorShapeDetails_) {
            vectorShapeDetails_->setPlainText(details.isEmpty() ? toQString(vectorShapePlaceholderText()) : details);
        }
        auto shape = std::optional<cast::ShapeInfo>{};
        if (runtimeMember && runtimeMember->isShape()) {
            shape = vectorShapeInfoFromMember(*runtimeMember);
        } else if (member && member->memberType() == cast::MemberType::Shape) {
            shape = cast::ShapeInfo::parse(member->specificData());
        }
        const auto* shapeInfo = shape ? &shape.value() : nullptr;
        if (shapeInfo != nullptr) {
            vectorShapeLoaded_ = true;
            vectorShapeEditorCastLib_ = castLib > 0 ? castLib : runtimeCastLib;
            vectorShapeEditorMemberNum_ = memberNum;
            const auto metadata = toQString(vectorShapeDetailsText(*shapeInfo));
            vectorShapeCanvasLabel_->setText({});
            vectorShapeCanvasLabel_->setPixmap(vectorShapePreviewPixmap(*shapeInfo));
            vectorShapeCanvasLabel_->setToolTip(metadata);
            if (vectorShapeDetails_) {
                vectorShapeDetails_->setPlainText(metadata);
            }
            vectorShapeStatus_->setText(toQString(vectorShapeLoadedStatus(loadedMemberName(), memberNum, shapeInfo)));
        } else if (hasLoadedMember) {
            vectorShapeCanvasLabel_->setText(details.isEmpty() ? toQString(vectorShapePlaceholderText()) : details);
            vectorShapeCanvasLabel_->setToolTip(details);
            vectorShapeStatus_->setText(toQString(vectorShapeLoadedStatus(loadedMemberName(), memberNum, nullptr)));
        } else {
            vectorShapeCanvasLabel_->setText(details.isEmpty() ? toQString(vectorShapePlaceholderText()) : details);
            vectorShapeCanvasLabel_->setToolTip(details);
            vectorShapeStatus_->setText(toQString(vectorShapePlaceholderText()));
        }
    }
    if (panelId == QStringLiteral("sound") && soundInfo_ && soundStatus_ && soundPlayButton_ && soundStopButton_ &&
        soundProgress_ && soundTimeLabel_) {
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
        if (soundPlayer_) {
            soundPlayer_->stop();
            soundPlayer_->setSource(QUrl());
        }
        soundTempFile_.reset();
        soundLoadedDuration_ = 0.0;
#endif
        const auto sound = sourceFile && member ? player::audio::SoundManager::findSoundForMember(*sourceFile, member) : nullptr;
        const auto duration = sound ? sound->durationSeconds() : 0.0;
        soundInfo_->setPlainText(details.isEmpty() ? toQString(soundWindowInitialText()) : details);
        soundStatus_->setText(hasLoadedMember ? toQString(soundWindowLoadedStatus(loadedMemberName(), memberNum, sound != nullptr, duration))
                                              : toQString(soundWindowReadyStatus()));
        const auto soundText = soundWindowText();
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
        bool playbackReady = false;
        if (sound && soundPlayer_) {
            if (const auto payload = soundExportPayload(*sound)) {
                auto tempFile = std::make_unique<QTemporaryFile>(
                    QDir(QDir::tempPath()).filePath(toQString(soundWindowTemporaryFileTemplate(payload->extension.toStdString()))));
                if (tempFile->open()) {
                    const auto written = tempFile->write(reinterpret_cast<const char*>(payload->bytes.data()),
                                                         static_cast<qint64>(payload->bytes.size()));
                    tempFile->flush();
                    if (written == static_cast<qint64>(payload->bytes.size())) {
                        soundLoadedDuration_ = duration;
                        soundPlayer_->setSource(QUrl::fromLocalFile(tempFile->fileName()));
                        soundTempFile_ = std::move(tempFile);
                        playbackReady = true;
                    }
                }
            }
        }
        soundPlayButton_->setEnabled(playbackReady);
        soundPlayButton_->setToolTip(playbackReady ? toQString(soundText.playButtonText)
                                                   : (sound ? toQString(soundWindowPlaybackUnavailableStatus()).trimmed()
                                                            : toQString(soundText.noDataTooltip)));
        soundStopButton_->setEnabled(playbackReady);
        soundStopButton_->setToolTip(playbackReady ? toQString(soundText.stopButtonText)
                                                   : (sound ? toQString(soundWindowPlaybackUnavailableStatus()).trimmed()
                                                            : QString{}));
#else
        soundPlayButton_->setEnabled(false);
        soundPlayButton_->setToolTip(sound ? toQString(soundWindowPlaybackUnavailableStatus()).trimmed()
                                           : toQString(soundText.noDataTooltip));
        soundStopButton_->setEnabled(false);
        soundStopButton_->setToolTip(sound ? toQString(soundWindowPlaybackUnavailableStatus()).trimmed()
                                           : QString{});
#endif
        soundProgress_->setValue(0);
        soundTimeLabel_->setText(sound ? toQString(soundWindowTimeText(0.0, duration))
                                       : toQString(soundWindowInitialTimeText()));
    }
    if (panelId == QStringLiteral("color-palettes") && colorPalettePreviewLabel_) {
        colorPalettePreviewLabel_->setPixmap({});
        colorPalettePreviewLabel_->setToolTip(details);
        if (colorPaletteInfo_) {
            colorPaletteInfo_->setPlainText(details.isEmpty() ? toQString(colorPalettePlaceholderText()) : details);
        }
        const auto palette = sourceFile && memberNum > 0 ? sourceFile->resolvePaletteByMemberNumber(memberNum) : nullptr;
        if (palette) {
            const auto swatch = buildPaletteSwatch(palette->colors());
            const auto paletteInfo = toQString(previewPaletteInfo(palette->colors()));
            colorPalettePreviewLabel_->setText({});
            colorPalettePreviewLabel_->setPixmap(paletteSwatchPixmap(swatch));
            colorPalettePreviewLabel_->setMinimumSize(swatch.width, swatch.height);
            colorPalettePreviewLabel_->setToolTip(paletteInfo);
            if (colorPaletteInfo_) {
                colorPaletteInfo_->setPlainText(paletteInfo);
            }
        } else {
            colorPalettePreviewLabel_->setText(details.isEmpty() ? toQString(colorPalettePlaceholderText()) : details);
            colorPalettePreviewLabel_->setMinimumSize(0, 0);
        }
    }
    showPanel(panelId);
}

QWidget* MainWindow::makePanelContent(const QString& title, const QStringList& lines) {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* heading = new QLabel(title, panel);
    heading->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(heading);
    if (title == QStringLiteral("Paint")) {
        auto* loaded = new QLabel(toQString(memberEditorNoCastMemberLoadedText()), panel);
        panelLoadLabels_.insert(QStringLiteral("paint"), loaded);
        layout->addWidget(loaded);
    } else if (title == QStringLiteral("Text")) {
        auto* loaded = new QLabel(toQString(memberEditorNoCastMemberLoadedText()), panel);
        panelLoadLabels_.insert(QStringLiteral("text"), loaded);
        layout->addWidget(loaded);
    } else if (title == QStringLiteral("Sound")) {
        auto* loaded = new QLabel(toQString(memberEditorNoCastMemberLoadedText()), panel);
        panelLoadLabels_.insert(QStringLiteral("sound"), loaded);
        layout->addWidget(loaded);
    } else if (title == QStringLiteral("Vector Shape")) {
        auto* loaded = new QLabel(toQString(memberEditorNoCastMemberLoadedText()), panel);
        panelLoadLabels_.insert(QStringLiteral("vector-shape"), loaded);
        layout->addWidget(loaded);
    }
    for (const auto& line : lines) {
        layout->addWidget(new QLabel(line, panel));
    }
    layout->addStretch(1);
    return panel;
}

QWidget* MainWindow::makeStagePanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* viewport = new QFrame(panel);
    viewport->setFrameShape(QFrame::NoFrame);
    viewport->setAutoFillBackground(true);
    auto viewportPalette = viewport->palette();
    viewportPalette.setColor(QPalette::Window, QColor(48, 48, 48));
    viewport->setPalette(viewportPalette);

    auto* viewportLayout = new QGridLayout(viewport);
    viewportLayout->setContentsMargins(12, 12, 12, 12);
    stageCanvasFrame_ = new QFrame(viewport);
    stageCanvasFrame_->setFrameShape(QFrame::Box);
    stageCanvasFrame_->setLineWidth(1);
    stageCanvasFrame_->setMinimumSize(stageWindowDefaultWidth(), stageWindowDefaultHeight());
    stageCanvasFrame_->setAutoFillBackground(true);
    stageCanvasFrame_->setMouseTracking(true);
    stageCanvasFrame_->setFocusPolicy(Qt::StrongFocus);
    stageCanvasFrame_->installEventFilter(this);
    auto canvasPalette = stageCanvasFrame_->palette();
    canvasPalette.setColor(QPalette::Window, Qt::lightGray);
    stageCanvasFrame_->setPalette(canvasPalette);

    auto* canvasLayout = new QVBoxLayout(stageCanvasFrame_);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    stageSummary_ = new QLabel(toQString(stageWindowNoMovieText()), stageCanvasFrame_);
    stageSummary_->setAlignment(Qt::AlignCenter);
    stageSummary_->setStyleSheet(QStringLiteral("color: #404040; font-weight: 600;"));
    stageSummary_->setWordWrap(true);
    stageSummary_->setMouseTracking(true);
    stageSummary_->setFocusPolicy(Qt::StrongFocus);
    stageSummary_->installEventFilter(this);
    canvasLayout->addWidget(stageSummary_, 1);

    viewportLayout->addWidget(stageCanvasFrame_, 0, 0, Qt::AlignCenter);
    layout->addWidget(viewport, 1);
    return panel;
}

QWidget* MainWindow::makeScorePanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    scoreSummary_ = new QLabel(toQString(mainWindowSummaryText().scorePlaceholderText), panel);

    auto* scoreArea = new QWidget(panel);
    auto* scoreAreaLayout = new QVBoxLayout(scoreArea);
    scoreAreaLayout->setContentsMargins(0, 0, 0, 0);

    auto* markerWithHeader = new QWidget(scoreArea);
    scoreMarkerRow_ = markerWithHeader;
    auto* markerLayout = new QHBoxLayout(markerWithHeader);
    markerLayout->setContentsMargins(0, 0, 0, 0);
    auto* markerSpacer = new QWidget(markerWithHeader);
    markerSpacer->setFixedWidth(scoreChannelHeaderWidth());
    scoreMarkers_ = new ScoreMarkerWidget(markerWithHeader);
    scoreMarkers_->setFrameShape(QFrame::StyledPanel);
    scoreMarkers_->setMarkerCallback([this](int frame) {
        context_.goToFrame(frame);
        context_.selectFrame(frame);
    });
    auto* markerScrollArea = new QScrollArea(markerWithHeader);
    markerScrollArea->setWidgetResizable(false);
    markerScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    markerScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    markerScrollArea->setFrameShape(QFrame::NoFrame);
    markerScrollArea->setFixedHeight(scoreHeaderHeight() + 2);
    markerScrollArea->setWidget(scoreMarkers_);
    markerLayout->addWidget(markerSpacer);
    markerLayout->addWidget(markerScrollArea, 1);

    auto* gridWithHeader = new QWidget(scoreArea);
    auto* gridLayout = new QHBoxLayout(gridWithHeader);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    scoreChannelHeader_ = new ScoreChannelHeaderWidget(gridWithHeader);
    scoreChannelHeader_->setFrameShape(QFrame::StyledPanel);
    scoreChannelHeader_->setChannelCallback([this](int channel) {
        context_.selectScoreCell(channel, context_.currentFrame());
    });
    auto* channelHeaderScrollArea = new QScrollArea(gridWithHeader);
    channelHeaderScrollArea->setWidgetResizable(false);
    channelHeaderScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channelHeaderScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channelHeaderScrollArea->setFrameShape(QFrame::NoFrame);
    channelHeaderScrollArea->setFixedWidth(scoreChannelHeaderWidth() + 2);
    channelHeaderScrollArea->setWidget(scoreChannelHeader_);

    scoreGrid_ = new ScoreGridWidget(scoreArea);
    scoreGrid_->setSelectionCallback([this](int channel, int frame) {
        context_.goToFrame(frame);
        context_.selectScoreCell(channel, frame);
    });
    scoreGrid_->setHoverCallback([this](std::optional<int> frame, std::optional<int> channel) {
        if (!scoreStatusLabel_) {
            return;
        }
        if (frame && channel) {
            scoreStatusLabel_->setText(toQString(scoreCellStatusText(*frame, *channel)));
            return;
        }
        const auto selection = context_.selection();
        if (selection.type == SelectionType::ScoreCell || selection.type == SelectionType::Sprite) {
            scoreStatusLabel_->setText(toQString(scoreCellStatusText(selection.frame, selection.channel)));
        } else {
            scoreStatusLabel_->setText(toQString(scoreFrameStatusText(context_.currentFrame())));
        }
    });
    auto* gridScrollArea = new QScrollArea(scoreArea);
    gridScrollArea->setWidgetResizable(false);
    gridScrollArea->setWidget(scoreGrid_);
    gridScrollArea->horizontalScrollBar()->setSingleStep(scoreCellWidth());
    gridScrollArea->verticalScrollBar()->setSingleStep(scoreCellHeight());
    gridScrollArea->setMinimumHeight(scoreHeaderHeight() + scoreCellHeight() * 8);
    connect(gridScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, markerScrollArea->horizontalScrollBar(), &QScrollBar::setValue);
    connect(markerScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, gridScrollArea->horizontalScrollBar(), &QScrollBar::setValue);
    connect(gridScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, channelHeaderScrollArea->verticalScrollBar(), &QScrollBar::setValue);
    connect(channelHeaderScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, gridScrollArea->verticalScrollBar(), &QScrollBar::setValue);

    scoreTable_ = new QTableWidget(0, 7, panel);
    QStringList scoreColumns;
    for (const auto column : scoreIntervalTableColumns()) {
        scoreColumns.push_back(toQString(column));
    }
    scoreTable_->setHorizontalHeaderLabels(scoreColumns);
    scoreTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scoreTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    scoreTable_->setAlternatingRowColors(true);
    scoreTable_->horizontalHeader()->setStretchLastSection(true);
    connect(scoreTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto selected = scoreTable_->selectedItems();
        if (selected.isEmpty()) {
            context_.clearSelection();
            return;
        }
        const auto* firstItem = scoreTable_->item(selected.first()->row(), 0);
        if (!firstItem) {
            context_.clearSelection();
            return;
        }
        const int frame = firstItem->data(Qt::UserRole).toInt();
        const int channel = firstItem->data(Qt::UserRole + 1).toInt();
        context_.goToFrame(frame);
        context_.selectScoreCell(channel, frame);
    });
    gridLayout->addWidget(channelHeaderScrollArea);
    gridLayout->addWidget(gridScrollArea, 1);
    scoreAreaLayout->addWidget(markerWithHeader);
    scoreAreaLayout->addWidget(gridWithHeader, 1);
    scoreAreaLayout->addWidget(scoreTable_, 1);

    scoreStatusLabel_ = new QLabel(toQString(scoreInitialStatusText()), panel);
    scoreStatusLabel_->setFrameShape(QFrame::StyledPanel);
    scoreStatusLabel_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(scoreSummary_);
    layout->addWidget(scoreArea, 1);
    layout->addWidget(scoreStatusLabel_);
    return panel;
}

QWidget* MainWindow::makeCastPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    const auto castText = castWindowText();

    castSelector_ = new QComboBox(toolbar);
    castSelector_->addItem(toQString(castWindowDefaultCastName()));
    toolbar->addWidget(castSelector_);
    auto* loadCastButton = new QPushButton(toQString(castText.loadCastText), toolbar);
    loadCastButton->setToolTip(toQString(castText.loadCastTooltip));
    toolbar->addWidget(loadCastButton);
    toolbar->addSeparator();

    castGridViewButton_ = new QPushButton(toQString(castText.gridViewText), toolbar);
    castGridViewButton_->setCheckable(true);
    const QString savedCastViewMode =
        EditorPreferences::preferenceString(QStringLiteral("cast"), QStringLiteral("viewMode"), QStringLiteral("Grid"));
    const bool castGridMode = savedCastViewMode != QStringLiteral("List");
    castGridViewButton_->setChecked(castGridMode);
    castGridViewButton_->setToolTip(toQString(castText.gridViewTooltip));
    castListViewButton_ = new QPushButton(toQString(castText.listViewText), toolbar);
    castListViewButton_->setCheckable(true);
    castListViewButton_->setChecked(!castGridMode);
    castListViewButton_->setToolTip(toQString(castText.listViewTooltip));
    toolbar->addWidget(castGridViewButton_);
    toolbar->addWidget(castListViewButton_);
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(toQString(castText.searchLabel), toolbar));
    castSearch_ = new QLineEdit(panel);
    castSearch_->setMaximumWidth(160);
    castSearch_->setPlaceholderText(toQString(castText.searchPlaceholder));
    toolbar->addWidget(castSearch_);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(toQString(castText.typeLabel), toolbar));
    castTypeFilter_ = new QComboBox(toolbar);
    for (const auto item : castWindowTypeFilterItems()) {
        castTypeFilter_->addItem(toQString(item));
    }
    const QString savedCastTypeFilter =
        EditorPreferences::preferenceString(QStringLiteral("cast"), QStringLiteral("typeFilter"), {});
    const int savedCastTypeFilterIndex = castTypeFilter_->findText(savedCastTypeFilter);
    if (savedCastTypeFilterIndex >= 0) {
        castTypeFilter_->setCurrentIndex(savedCastTypeFilterIndex);
    }
    toolbar->addWidget(castTypeFilter_);

    castSummary_ = new QLabel(toQString(castText.emptySummaryText), panel);
    memberSummary_ = new QPlainTextEdit(panel);
    memberSummary_->setReadOnly(true);
    memberSummary_->setPlainText(toQString(castText.selectedMemberPlaceholderText));
    memberSummary_->setMinimumHeight(120);
    memberSummary_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont memberSummaryFont(QStringLiteral("Courier New"));
    memberSummaryFont.setStyleHint(QFont::Monospace);
    memberSummary_->setFont(memberSummaryFont);
    castTable_ = new QTableWidget(0, 6, panel);
    QStringList castColumns;
    for (const auto column : castWindowTableColumns()) {
        castColumns.push_back(toQString(column));
    }
    castTable_->setHorizontalHeaderLabels(castColumns);
    castTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    castTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    castTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    castTable_->setAlternatingRowColors(true);
    castTable_->setIconSize(QSize(castThumbnailSize(), castThumbnailSize()));
    castTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    castTable_->horizontalHeader()->setStretchLastSection(true);
    castTable_->setVisible(false);
    castGrid_ = new QListWidget(panel);
    castGrid_->setViewMode(QListView::IconMode);
    castGrid_->setMovement(QListView::Static);
    castGrid_->setResizeMode(QListView::Adjust);
    castGrid_->setWrapping(true);
    castGrid_->setFlow(QListView::LeftToRight);
    castGrid_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    castGrid_->setIconSize(QSize(castThumbnailSize(), castThumbnailSize()));
    castGrid_->setGridSize(QSize(castThumbnailSize() + 40, castThumbnailSize() + 42));
    castGrid_->setWordWrap(true);
    castGrid_->setContextMenuPolicy(Qt::CustomContextMenu);
    castGrid_->setVisible(true);
    openCastMemberButton_ = new QPushButton(toQString(castWindowOpenSelectedButtonText()), panel);
    openCastMemberButton_->setEnabled(false);
    connect(castSearch_, &QLineEdit::textChanged, this, [this] { updateMovieViews(); });
    connect(castTypeFilter_, &QComboBox::currentTextChanged, this, [this](const QString& value) {
        EditorPreferences::setPreferenceString(QStringLiteral("cast"), QStringLiteral("typeFilter"), value);
        updateMovieViews();
    });
    connect(castSelector_, &QComboBox::currentIndexChanged, this, [this] {
        castThumbnailCache_.clear();
        updateMovieViews();
    });
    connect(loadCastButton, &QPushButton::clicked, this, [this] { loadSelectedExternalCast(); });
    connect(castGridViewButton_, &QPushButton::clicked, this, [this] {
        castGridViewButton_->setChecked(true);
        castListViewButton_->setChecked(false);
        EditorPreferences::setPreferenceString(QStringLiteral("cast"), QStringLiteral("viewMode"), QStringLiteral("Grid"));
        updateMovieViews();
        if (castStatusLabel_) {
            castStatusLabel_->setText(toQString(castWindowGridViewModeSpec().statusText));
        }
    });
    connect(castListViewButton_, &QPushButton::clicked, this, [this] {
        castListViewButton_->setChecked(true);
        castGridViewButton_->setChecked(false);
        EditorPreferences::setPreferenceString(QStringLiteral("cast"), QStringLiteral("viewMode"), QStringLiteral("List"));
        updateMovieViews();
        if (castStatusLabel_) {
            castStatusLabel_->setText(toQString(castWindowListViewModeSpec().statusText));
        }
    });
    connect(castTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto selected = castTable_->selectedItems();
        if (selected.isEmpty()) {
            memberSummary_->setPlainText(toQString(castWindowText().selectedMemberPlaceholderText));
            openCastMemberButton_->setEnabled(false);
            context_.clearSelection();
            return;
        }
        const int row = selected.first()->row();
        const auto* firstItem = castTable_->item(row, 0);
        const QString details = firstItem ? firstItem->data(kCastDetailsRole).toString() : QString();
        const QString targetPanel = firstItem ? firstItem->data(kCastTargetPanelRole).toString() : QString();
        const int memberNum = firstItem ? firstItem->data(kCastMemberNumberRole).toInt() : 0;
        const int castLib = firstItem ? firstItem->data(kCastLibraryNumberRole).toInt() : 0;
        memberSummary_->setPlainText(details.isEmpty() ? toQString(castWindowText().selectedMemberPlaceholderText) : details);
        openCastMemberButton_->setEnabled(!targetPanel.isEmpty());
        context_.selectCastMember(castLib, memberNum);
    });
    connect(castGrid_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto selected = castGrid_->selectedItems();
        if (selected.isEmpty()) {
            memberSummary_->setPlainText(toQString(castWindowText().selectedMemberPlaceholderText));
            openCastMemberButton_->setEnabled(false);
            context_.clearSelection();
            return;
        }
        auto* firstItem = selected.first();
        const QString details = firstItem ? firstItem->data(kCastDetailsRole).toString() : QString();
        const QString targetPanel = firstItem ? firstItem->data(kCastTargetPanelRole).toString() : QString();
        const int memberNum = firstItem ? firstItem->data(kCastMemberNumberRole).toInt() : 0;
        const int castLib = firstItem ? firstItem->data(kCastLibraryNumberRole).toInt() : 0;
        memberSummary_->setPlainText(details.isEmpty() ? toQString(castWindowText().selectedMemberPlaceholderText) : details);
        openCastMemberButton_->setEnabled(!targetPanel.isEmpty());
        context_.selectCastMember(castLib, memberNum);
    });
    connect(openCastMemberButton_, &QPushButton::clicked, this, [this] {
        openSelectedCastMember();
    });
    connect(castTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        castTable_->selectRow(row);
        openSelectedCastMember();
    });
    connect(castGrid_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        castGrid_->setCurrentItem(item);
        openSelectedCastMember();
    });
    auto* openSelectedShortcut = new QShortcut(QKeySequence(Qt::Key_Return), castTable_);
    connect(openSelectedShortcut, &QShortcut::activated, this, [this] { openSelectedCastMember(); });
    auto* openSelectedEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), castTable_);
    connect(openSelectedEnterShortcut, &QShortcut::activated, this, [this] { openSelectedCastMember(); });
    auto* openGridShortcut = new QShortcut(QKeySequence(Qt::Key_Return), castGrid_);
    connect(openGridShortcut, &QShortcut::activated, this, [this] { openSelectedCastMember(); });
    auto* openGridEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), castGrid_);
    connect(openGridEnterShortcut, &QShortcut::activated, this, [this] { openSelectedCastMember(); });
    connect(castTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const auto selectedRanges = castTable_->selectedRanges();
        int selectedRows = 0;
        for (const auto& range : selectedRanges) {
            selectedRows += range.rowCount();
        }
        const int row = castTable_->rowAt(position.y());
        auto* rowItem = row >= 0 ? castTable_->item(row, 0) : nullptr;
        if (row >= 0 && !castTable_->selectionModel()->isRowSelected(row, QModelIndex())) {
            castTable_->selectRow(row);
            selectedRows = 1;
            rowItem = castTable_->item(row, 0);
        }

        QMenu menu(this);
        if (rowItem) {
            const int memberNum = rowItem->data(kCastMemberNumberRole).toInt();
            const int castLib = rowItem->data(kCastLibraryNumberRole).toInt();
            const QString targetPanel = rowItem->data(kCastTargetPanelRole).toString();
            if (!targetPanel.isEmpty()) {
                auto* openAction = menu.addAction(toQString(castWindowText().openText));
                connect(openAction, &QAction::triggered, this, [this] { openSelectedCastMember(); });
                if (targetPanel == QStringLiteral("text")) {
                    const QString details = rowItem->data(kCastDetailsRole).toString();
                    auto* openFieldAction = menu.addAction(toQString(castWindowText().openInFieldText));
                    connect(openFieldAction, &QAction::triggered, this, [this, details, castLib, memberNum] {
                        loadCastMemberIntoPanel(QStringLiteral("field"), details, castLib, memberNum);
                    });
                }
                menu.addSeparator();
            }
            auto* exportAction = menu.addAction(toQString(castWindowExportActionText()));
            connect(exportAction, &QAction::triggered, this, [this, castLib, memberNum] {
                QString defaultName = toQString(castMemberFallbackExportFileName(memberNum));
                if (const auto sourceFile = sourceFileForCastLib(castLib)) {
                    if (const auto member = castMemberForCastLib(castLib, memberNum)) {
                        const auto row = castMemberRowFromChunk(memberNum, *member);
                        defaultName = castMemberExportFileNameForMovie(*sourceFile, row, member);
                    }
                }
                const QString path = QFileDialog::getSaveFileName(
                    this,
                    toQString(castWindowText().exportMemberTitle),
                    defaultName,
                    toQString(castWindowText().supportedExportFileFilter));
                if (path.isEmpty()) {
                    return;
                }
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(castWindowExportStartingStatus(1)));
                }
                const bool exported = exportCastMemberToFile(castLib, memberNum, path);
                if (castStatusLabel_) {
                    castStatusLabel_->setText(
                        toQString(exported ? castWindowExportedStatus(1) : castWindowExportFailedStatus(1)));
                }
            });
        }
        if (selectedRows > 1) {
            auto* exportSelected = menu.addAction(toQString(castWindowExportSelectedActionText()));
            connect(exportSelected, &QAction::triggered, this, [this] {
                const auto memberRefs = selectedCastMemberRefs();
                if (memberRefs.isEmpty()) {
                    return;
                }
                const QString directory = QFileDialog::getExistingDirectory(
                    this,
                    toQString(castWindowText().exportAllSelectedDirectoryTitle));
                if (directory.isEmpty()) {
                    return;
                }
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(castWindowExportStartingStatus(memberRefs.size())));
                }
                const int exported = exportCastMembersToDirectory(memberRefs, directory);
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(exported > 0 ? castWindowExportedStatus(exported)
                                                                     : castWindowExportFailedStatus(memberRefs.size())));
                }
            });
        } else {
            auto* selectAll = menu.addAction(toQString(castWindowSelectAllActionText()));
            selectAll->setEnabled(castTable_->rowCount() > 0);
            connect(selectAll, &QAction::triggered, this, [this] {
                castTable_->selectAll();
            });
        }
        if (rowItem) {
            auto* copyName = menu.addAction(toQString(castWindowCopyNameActionText()));
            connect(copyName, &QAction::triggered, this, [this, row] {
                if (auto* nameItem = castTable_->item(row, 3)) {
                    QApplication::clipboard()->setText(nameItem->text());
                }
            });
        }
        if (!menu.actions().empty()) {
            menu.exec(castTable_->viewport()->mapToGlobal(position));
        }
    });
    connect(castGrid_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* clickedItem = castGrid_->itemAt(position);
        if (clickedItem && !clickedItem->isSelected()) {
            castGrid_->clearSelection();
            clickedItem->setSelected(true);
            castGrid_->setCurrentItem(clickedItem);
        }
        auto* currentItem = clickedItem ? clickedItem : castGrid_->currentItem();
        const auto selectedItems = castGrid_->selectedItems();
        QMenu menu(this);
        if (currentItem) {
            const int memberNum = currentItem->data(kCastMemberNumberRole).toInt();
            const int castLib = currentItem->data(kCastLibraryNumberRole).toInt();
            const QString targetPanel = currentItem->data(kCastTargetPanelRole).toString();
            if (!targetPanel.isEmpty()) {
                auto* openAction = menu.addAction(toQString(castWindowText().openText));
                connect(openAction, &QAction::triggered, this, [this] { openSelectedCastMember(); });
                if (targetPanel == QStringLiteral("text")) {
                    const QString details = currentItem->data(kCastDetailsRole).toString();
                    auto* openFieldAction = menu.addAction(toQString(castWindowText().openInFieldText));
                    connect(openFieldAction, &QAction::triggered, this, [this, details, castLib, memberNum] {
                        loadCastMemberIntoPanel(QStringLiteral("field"), details, castLib, memberNum);
                    });
                }
                menu.addSeparator();
            }
            auto* exportAction = menu.addAction(toQString(castWindowExportActionText()));
            connect(exportAction, &QAction::triggered, this, [this, castLib, memberNum] {
                QString defaultName = toQString(castMemberFallbackExportFileName(memberNum));
                if (const auto sourceFile = sourceFileForCastLib(castLib)) {
                    if (const auto member = castMemberForCastLib(castLib, memberNum)) {
                        const auto row = castMemberRowFromChunk(memberNum, *member);
                        defaultName = castMemberExportFileNameForMovie(*sourceFile, row, member);
                    }
                }
                const QString path = QFileDialog::getSaveFileName(
                    this,
                    toQString(castWindowText().exportMemberTitle),
                    defaultName,
                    toQString(castWindowText().supportedExportFileFilter));
                if (path.isEmpty()) {
                    return;
                }
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(castWindowExportStartingStatus(1)));
                }
                const bool exported = exportCastMemberToFile(castLib, memberNum, path);
                if (castStatusLabel_) {
                    castStatusLabel_->setText(
                        toQString(exported ? castWindowExportedStatus(1) : castWindowExportFailedStatus(1)));
                }
            });
        }
        if (selectedItems.size() > 1) {
            auto* exportSelected = menu.addAction(toQString(castWindowExportSelectedActionText()));
            connect(exportSelected, &QAction::triggered, this, [this] {
                const auto memberRefs = selectedCastMemberRefs();
                if (memberRefs.isEmpty()) {
                    return;
                }
                const QString directory = QFileDialog::getExistingDirectory(
                    this,
                    toQString(castWindowText().exportAllSelectedDirectoryTitle));
                if (directory.isEmpty()) {
                    return;
                }
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(castWindowExportStartingStatus(memberRefs.size())));
                }
                const int exported = exportCastMembersToDirectory(memberRefs, directory);
                if (castStatusLabel_) {
                    castStatusLabel_->setText(toQString(exported > 0 ? castWindowExportedStatus(exported)
                                                                     : castWindowExportFailedStatus(memberRefs.size())));
                }
            });
        } else {
            auto* selectAll = menu.addAction(toQString(castWindowSelectAllActionText()));
            selectAll->setEnabled(castGrid_->count() > 0);
            connect(selectAll, &QAction::triggered, this, [this] {
                castGrid_->selectAll();
            });
        }
        if (currentItem) {
            auto* copyName = menu.addAction(toQString(castWindowCopyNameActionText()));
            connect(copyName, &QAction::triggered, this, [this, currentItem] {
                QApplication::clipboard()->setText(currentItem->data(kCastDisplayNameRole).toString());
            });
        }
        if (!menu.actions().empty()) {
            menu.exec(castGrid_->viewport()->mapToGlobal(position));
        }
    });
    auto* selectAllShortcut = new QShortcut(QKeySequence::SelectAll, castTable_);
    connect(selectAllShortcut, &QShortcut::activated, this, [this] {
        castTable_->selectAll();
    });
    auto* gridSelectAllShortcut = new QShortcut(QKeySequence::SelectAll, castGrid_);
    connect(gridSelectAllShortcut, &QShortcut::activated, this, [this] {
        castGrid_->selectAll();
    });
    auto* copyNameShortcut = new QShortcut(QKeySequence::Copy, castTable_);
    connect(copyNameShortcut, &QShortcut::activated, this, [this] {
        const auto selected = castTable_->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        const int row = selected.first()->row();
        if (auto* nameItem = castTable_->item(row, 3)) {
            QApplication::clipboard()->setText(nameItem->text());
        }
    });
    auto* gridCopyNameShortcut = new QShortcut(QKeySequence::Copy, castGrid_);
    connect(gridCopyNameShortcut, &QShortcut::activated, this, [this] {
        auto* item = castGrid_->currentItem();
        if (item) {
            QApplication::clipboard()->setText(item->data(kCastDisplayNameRole).toString());
        }
    });
    auto* openShortcut = new QShortcut(QKeySequence(Qt::Key_Return), castTable_);
    connect(openShortcut, &QShortcut::activated, this, [this] {
        if (openCastMemberButton_ && openCastMemberButton_->isEnabled()) {
            openCastMemberButton_->click();
        }
    });
    auto* exportShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), castTable_);
    connect(exportShortcut, &QShortcut::activated, this, [this] { exportSelectedCastMembers(); });
    castStatusLabel_ = new QLabel(toQString(castWindowReadyStatus()), panel);
    castStatusLabel_->setFrameShape(QFrame::StyledPanel);
    castStatusLabel_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(toolbar);
    layout->addWidget(castSummary_);
    layout->addWidget(castGrid_, 1);
    layout->addWidget(castTable_, 1);
    layout->addWidget(memberSummary_);
    layout->addWidget(openCastMemberButton_);
    layout->addWidget(castStatusLabel_);
    return panel;
}

QWidget* MainWindow::makePropertyInspectorPanel() {
    auto* tabs = new QTabWidget;
    const auto tabNames = propertyInspectorTabNames();
    const auto inspectorText = propertyInspectorText();
    auto* memberPanel = new QWidget(tabs);
    auto* memberLayout = new QVBoxLayout(memberPanel);
    selectionSummary_ = new QLabel(toQString(inspectorText.noSelectionText), memberPanel);
    selectionSummary_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    selectedMemberDetails_ = new QLabel(toQString(inspectorText.memberPlaceholderText), memberPanel);
    selectedMemberDetails_->setWordWrap(true);
    memberLayout->addWidget(selectionSummary_);
    memberLayout->addWidget(selectedMemberDetails_);
    memberLayout->addWidget(makePropertyGrid(propertyInspectorMemberLabels(), memberPanel, &propertyMemberFields_), 1);

    auto* behaviorPanel = new QWidget(tabs);
    auto* behaviorLayout = new QVBoxLayout(behaviorPanel);
    auto* behaviorButtons = new QWidget(behaviorPanel);
    auto* behaviorButtonLayout = new QHBoxLayout(behaviorButtons);
    behaviorButtonLayout->setContentsMargins(0, 0, 0, 0);
    auto* addBehavior = new QPushButton(toQString(inspectorText.addBehaviorText), behaviorButtons);
    auto* removeBehavior = new QPushButton(toQString(inspectorText.removeBehaviorText), behaviorButtons);
    removeBehavior->setEnabled(false);
    auto* openBehavior = new QPushButton(toQString(inspectorText.openScriptText), behaviorButtons);
    openBehavior->setEnabled(false);
    behaviorButtonLayout->addWidget(addBehavior);
    behaviorButtonLayout->addWidget(removeBehavior);
    behaviorButtonLayout->addWidget(openBehavior);
    behaviorButtonLayout->addStretch(1);
    behaviorList_ = new QListWidget(behaviorPanel);
    behaviorList_->addItem(toQString(propertyInspectorBehaviorPlaceholderText()));
    behaviorList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(behaviorList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        openSelectedBehaviorScript();
    });
    connect(behaviorList_, &QListWidget::currentItemChanged, this, [openBehavior, removeBehavior](QListWidgetItem* current) {
        openBehavior->setEnabled(current && current->data(kBehaviorScriptIdRole).toInt() > 0);
        const bool pendingRemoval = current && current->data(kBehaviorPendingActionRole).toInt() == 2;
        removeBehavior->setEnabled(current && !pendingRemoval && !current->text().startsWith(QLatin1Char('(')));
    });
    connect(behaviorList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = behaviorList_ ? behaviorList_->itemAt(pos) : nullptr;
        if (!item || item->data(kBehaviorScriptIdRole).toInt() <= 0) {
            return;
        }
        behaviorList_->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(toQString(propertyInspectorText().openBehaviorScriptText), this, [this]() {
            openSelectedBehaviorScript();
        });
        menu.exec(behaviorList_->viewport()->mapToGlobal(pos));
    });
    connect(addBehavior, &QPushButton::clicked, this, &MainWindow::prepareAddBehaviorToSelectedSprite);
    connect(removeBehavior, &QPushButton::clicked, this, &MainWindow::prepareRemoveSelectedBehavior);
    connect(openBehavior, &QPushButton::clicked, this, &MainWindow::openSelectedBehaviorScript);
    behaviorLayout->addWidget(behaviorButtons);
    behaviorLayout->addWidget(behaviorList_, 1);

    auto* moviePanel = new QWidget(tabs);
    auto* movieLayout = new QVBoxLayout(moviePanel);
    movieSummary_ = new QLabel(toQString(inspectorText.moviePlaceholderText), moviePanel);
    movieSummary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    movieLayout->addWidget(movieSummary_);
    movieLayout->addWidget(makePropertyGrid(propertyInspectorMovieLabels(), moviePanel, &propertyMovieFields_), 1);

    tabs->addTab(makePropertyGrid(propertyInspectorSpriteLabels(), tabs, &propertySpriteFields_), toQString(tabNames[0]));
    tabs->addTab(memberPanel, toQString(tabNames[1]));
    tabs->addTab(behaviorPanel, toQString(tabNames[2]));
    tabs->addTab(moviePanel, toQString(tabNames[3]));
    return tabs;
}

QWidget* MainWindow::makeScriptPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* selectorBar = new QWidget(panel);
    auto* selectorLayout = new QHBoxLayout(selectorBar);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    const auto scriptText = scriptEditorText();
    selectorLayout->addWidget(new QLabel(toQString(scriptText.castLabel), selectorBar));
    scriptCastSelector_ = new QComboBox(selectorBar);
    scriptCastSelector_->setMaximumWidth(200);
    scriptCastSelector_->addItem(toQString(scriptEditorDefaultCastName()));
    selectorLayout->addWidget(scriptCastSelector_);
    selectorLayout->addSpacing(8);
    selectorLayout->addWidget(new QLabel(toQString(scriptText.scriptLabel), selectorBar));
    scriptSelector_ = new QComboBox(selectorBar);
    scriptSelector_->setMinimumWidth(240);
    selectorLayout->addWidget(scriptSelector_, 1);
    selectorLayout->addSpacing(8);
    selectorLayout->addWidget(new QLabel(toQString(scriptText.handlerLabel), selectorBar));
    scriptHandlerSelector_ = new QComboBox(selectorBar);
    scriptHandlerSelector_->setMinimumWidth(160);
    selectorLayout->addWidget(scriptHandlerSelector_);
    selectorLayout->addSpacing(12);
    scriptLingoToggle_ = new QPushButton(toQString(scriptText.lingoToggleText), selectorBar);
    scriptLingoToggle_->setCheckable(true);
    const QString scriptDefaultView =
        EditorPreferences::preferenceString(QStringLiteral("script"), QStringLiteral("defaultView"), QStringLiteral("Lingo"));
    scriptLingoToggle_->setChecked(scriptDefaultView != QStringLiteral("Bytecode"));
    scriptLingoToggle_->setText(toQString(scriptLingoToggle_->isChecked() ? scriptText.bytecodeToggleText
                                                                          : scriptText.lingoToggleText));
    scriptLingoToggle_->setToolTip(toQString(scriptText.lingoToggleTooltip));
    selectorLayout->addWidget(scriptLingoToggle_);
    selectorLayout->addStretch(1);

    scriptSummary_ = new QLabel(toQString(scriptText.emptySummaryText), panel);
    auto* loadedMember = new QLabel(toQString(memberEditorNoCastMemberLoadedText()), panel);
    panelLoadLabels_.insert(QStringLiteral("script"), loadedMember);
    scriptTable_ = new QTableWidget(0, 6, panel);
    scriptTable_->setHorizontalHeaderLabels(toQStringList(scriptEditorTableColumns()));
    scriptTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scriptTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    scriptTable_->setAlternatingRowColors(true);
    scriptTable_->horizontalHeader()->setStretchLastSection(true);
    scriptPreview_ = new QPlainTextEdit(panel);
    scriptPreview_->setReadOnly(true);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(13);
    scriptPreview_->setFont(monoFont);
    const int scriptTabWidth =
        std::clamp(EditorPreferences::preferenceInt(QStringLiteral("script"), QStringLiteral("tabWidth"), 4), 1, 16);
    scriptPreview_->setTabStopDistance(scriptPreview_->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * scriptTabWidth);
    if (EditorPreferences::preferenceBool(QStringLiteral("script"), QStringLiteral("syntaxHighlighting"), true)) {
        new ScriptPreviewHighlighter(scriptPreview_->document());
    }
    scriptPreview_->setPlainText(toQString(scriptEditorInitialText()));
    connect(scriptCastSelector_, &QComboBox::currentIndexChanged, this, [this](int) { updateMovieViews(); });
    connect(scriptSelector_, &QComboBox::currentIndexChanged, this, [this](int) { updateScriptEditorPreview(); });
    connect(scriptHandlerSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto bytecodePreviews = scriptSelector_ ? scriptSelector_->currentData().toStringList() : QStringList{};
        const auto lingoPreviews = scriptSelector_ ? scriptSelector_->currentData(Qt::UserRole + 3).toStringList() : QStringList{};
        const bool showLingo = scriptLingoToggle_ && scriptLingoToggle_->isChecked() && !lingoPreviews.isEmpty();
        QStringList handlerPreviews = showLingo ? lingoPreviews : bytecodePreviews;
        if (!showLingo) {
            for (QString& handlerPreview : handlerPreviews) {
                handlerPreview = bytecodePreviewWithAnnotationPreference(handlerPreview);
            }
        }
        if (handlerPreviews.isEmpty()) {
            scriptPreview_->setPlainText(toQString(scriptEditorNoHandlersText()));
        } else if (index <= 0) {
            scriptPreview_->setPlainText(handlerPreviews.join(QStringLiteral("\n")));
        } else if (index - 1 < handlerPreviews.size()) {
            scriptPreview_->setPlainText(handlerPreviews.at(index - 1));
        }
    });
    connect(scriptLingoToggle_, &QPushButton::toggled, this, [this](bool lingo) {
        EditorPreferences::setPreferenceString(QStringLiteral("script"),
                                               QStringLiteral("defaultView"),
                                               lingo ? QStringLiteral("Lingo") : QStringLiteral("Bytecode"));
        const auto scriptText = scriptEditorText();
        scriptLingoToggle_->setText(toQString(lingo ? scriptText.bytecodeToggleText : scriptText.lingoToggleText));
        updateScriptEditorPreview();
    });
    connect(scriptTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto selected = scriptTable_->selectedItems();
        if (selected.isEmpty()) {
            scriptPreview_->setPlainText(toQString(scriptEditorInitialText()));
            return;
        }
        const int row = selected.first()->row();
        const auto* idItem = scriptTable_->item(row, 0);
        if (idItem && scriptSelector_) {
            bool ok = false;
            const int scriptId = idItem->data(Qt::UserRole + 1).toInt(&ok);
            if (ok) {
                for (int index = 0; index < scriptSelector_->count(); ++index) {
                    bool selectorOk = false;
                    const int itemScriptId = scriptSelector_->itemData(index, Qt::UserRole + 1).toInt(&selectorOk);
                    if (selectorOk && itemScriptId == scriptId) {
                        const bool blocked = scriptSelector_->blockSignals(true);
                        scriptSelector_->setCurrentIndex(index);
                        scriptSelector_->blockSignals(blocked);
                        break;
                    }
                }
            }
        }
        updateScriptEditorPreview();
    });
    layout->addWidget(selectorBar);
    layout->addWidget(scriptSummary_);
    layout->addWidget(loadedMember);
    layout->addWidget(scriptTable_, 1);
    layout->addWidget(scriptPreview_, 1);
    return panel;
}

QWidget* MainWindow::makeMessagePanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);

    messageOutput_ = new QPlainTextEdit(panel);
    messageOutput_->setReadOnly(true);
    messageOutput_->setFont(monoFont);
    messageOutput_->setPlainText(toQString(messageWindowWelcomeText()));

    messageInput_ = new QLineEdit(panel);
    messageInput_->setFont(monoFont);
    connect(messageInput_, &QLineEdit::returnPressed, this, [this] {
        const auto command = messageInput_->text().toStdString();
        if (messageWindowCommandTranscript(command).empty()) {
            return;
        }
        if (messageWindowIsClearCommand(command)) {
            messageOutput_->setPlainText(toQString(messageWindowWelcomeText()));
            messageInput_->clear();
            return;
        }
        QString transcript;
        try {
            if (!stagePlayer_) {
                transcript = toQString(messageWindowErrorTranscript(command, "No movie loaded"));
            } else {
                const auto expression = messageWindowExpressionForCommand(command);
                if (!expression) {
                    messageInput_->clear();
                    return;
                }
                const auto result = lingo::vm::parse::LingoExpressionParser::parse(*expression, &stagePlayer_->vm());
                transcript = toQString(messageWindowResultTranscript(command, result));
            }
        } catch (const std::exception& ex) {
            transcript = toQString(messageWindowErrorTranscript(command, ex.what()));
        }
        messageOutput_->moveCursor(QTextCursor::End);
        messageOutput_->insertPlainText(transcript);
        messageInput_->clear();
        messageOutput_->moveCursor(QTextCursor::End);
    });

    layout->addWidget(messageOutput_, 1);
    layout->addWidget(messageInput_);
    return panel;
}

QWidget* MainWindow::makeToolPalettePanel() {
    auto* panel = new QWidget;
    auto* root = new QVBoxLayout(panel);
    auto* layout = new QGridLayout;
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    toolButtons_ = new QButtonGroup(panel);
    toolButtons_->setExclusive(true);
    int row = 0;
    int col = 0;
    int id = 0;
    for (const auto tool : toolPaletteTools()) {
        auto* button = new QPushButton(toQString(tool), panel);
        button->setCheckable(true);
        button->setMinimumWidth(64);
        button->setToolTip(toQString(toolPaletteToolTipText(tool)));
        toolButtons_->addButton(button, id++);
        layout->addWidget(button, row, col);
        col = (col + 1) % 2;
        if (col == 0) {
            ++row;
        }
    }
    activeToolLabel_ = new QLabel(toQString(toolPaletteActiveToolText("Arrow")), panel);
    root->addLayout(layout);
    root->addWidget(activeToolLabel_);
    root->addStretch(1);
    int restoredToolId = 0;
    const QString savedTool =
        EditorPreferences::preferenceString(QStringLiteral("toolPalette"), QStringLiteral("selectedTool"), QStringLiteral("Arrow"));
    const auto tools = toolPaletteTools();
    for (std::size_t index = 0; index < tools.size(); ++index) {
        if (toQString(tools[index]) == savedTool) {
            restoredToolId = static_cast<int>(index);
            break;
        }
    }
    activeToolId_ = restoredToolId;
    if (auto* restored = toolButtons_->button(restoredToolId)) {
        restored->setChecked(true);
        activeToolLabel_->setText(toQString(toolPaletteActiveToolText(restored->text().toStdString())));
    }
    connect(toolButtons_, &QButtonGroup::idClicked, this, [this](int id) {
        const auto tools = toolPaletteTools();
        if (id < 0 || id >= static_cast<int>(tools.size())) {
            return;
        }
        activeToolId_ = id;
        const QString tool = toQString(tools[static_cast<std::size_t>(id)]);
        EditorPreferences::setPreferenceString(QStringLiteral("toolPalette"), QStringLiteral("selectedTool"), tool);
        activeToolLabel_->setText(toQString(toolPaletteActiveToolText(tools[static_cast<std::size_t>(id)])));
        statusLabel_->setText(toQString(toolPaletteSelectedStatusText(tools[static_cast<std::size_t>(id)])));
    });
    return panel;
}

QWidget* MainWindow::makePaintPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);
    paintBrushSize_ =
        std::clamp(EditorPreferences::preferenceInt(QStringLiteral("paint"), QStringLiteral("brushSize"), 3), 1, 128);
    paintAntialiasPreview_ =
        EditorPreferences::preferenceBool(QStringLiteral("paint"), QStringLiteral("antialiasPreview"), true);
    paintShowTransparencyGrid_ =
        EditorPreferences::preferenceBool(QStringLiteral("paint"), QStringLiteral("showTransparencyGrid"), true);
    QAction* restoredPaintTool = nullptr;
    const QString savedPaintTool =
        EditorPreferences::preferenceString(QStringLiteral("paint"), QStringLiteral("selectedTool"), QStringLiteral("Pencil"));
    for (std::size_t i = 0; i < paintWindowTools().size(); ++i) {
        if (i == 7) {
            toolbar->addSeparator();
        }
        const QString toolName = toQString(paintWindowTools()[i]);
        auto* action = toolbar->addAction(toolName);
        action->setCheckable(true);
        action->setData(toolName);
        action->setToolTip(toQString(paintWindowToolTipText(toolName.toStdString())));
        toolGroup->addAction(action);
        if (toolName == savedPaintTool) {
            restoredPaintTool = action;
        }
        connect(action, &QAction::triggered, this, [this, toolName] {
            EditorPreferences::setPreferenceString(QStringLiteral("paint"), QStringLiteral("selectedTool"), toolName);
            const QString status =
                toQString(paintWindowToolStatus(toolName.toStdString(), !paintOriginalPixmap_.isNull(), paintBrushSize_));
            if (paintStatus_) {
                paintStatus_->setText(status);
            }
            statusLabel_->setText(status.trimmed());
        });
    }
    if (restoredPaintTool) {
        restoredPaintTool->setChecked(true);
    }
    toolbar->addSeparator();
    auto* colorAction = toolbar->addAction(toQString(paintWindowColorActionLabel()));
    colorAction->setToolTip(toQString(paintWindowColorActionTooltip()));
    connect(colorAction, &QAction::triggered, this, [this] {
        const QColor selected = QColorDialog::getColor(paintDrawColor_, this, QStringLiteral("Choose Paint Color"));
        if (!selected.isValid()) {
            return;
        }
        paintDrawColor_ = selected;
        const QString status = toQString(paintWindowColorStatus(selected.red(), selected.green(), selected.blue()));
        if (paintStatus_) {
            paintStatus_->setText(status);
        }
        if (statusLabel_) {
            statusLabel_->setText(status.trimmed());
        }
    });
    toolbar->addSeparator();
    for (const auto& command : paintWindowViewActionSpecs()) {
        auto* action = toolbar->addAction(toQString(command.label));
        action->setToolTip(toQString(command.tooltip));
        if (command.id == "actual-size") {
            connect(action, &QAction::triggered, this, [this] {
                setPaintZoom(1.0);
            });
        } else if (command.id == "fit") {
            connect(action, &QAction::triggered, this, [this] {
                fitPaintToView();
            });
        } else if (command.id == "zoom-out") {
            connect(action, &QAction::triggered, this, [this] {
                setPaintZoom(paintZoom_ / 1.25);
            });
        } else if (command.id == "zoom-in") {
            connect(action, &QAction::triggered, this, [this] {
                setPaintZoom(paintZoom_ * 1.25);
            });
        }
    }

    paintImageLabel_ = new QLabel(toQString(paintWindowInitialText()), panel);
    paintImageLabel_->setAlignment(Qt::AlignCenter);
    paintImageLabel_->setWordWrap(true);
    paintImageLabel_->setMinimumSize(320, 240);
    paintImageLabel_->installEventFilter(this);
    paintImageLabel_->setMouseTracking(false);

    auto* scrollArea = new QScrollArea(panel);
    paintScrollArea_ = scrollArea;
    scrollArea->setWidgetResizable(false);
    scrollArea->setWidget(paintImageLabel_);
    scrollArea->viewport()->setAutoFillBackground(true);
    auto viewportPalette = scrollArea->viewport()->palette();
    viewportPalette.setColor(QPalette::Window, Qt::darkGray);
    scrollArea->viewport()->setPalette(viewportPalette);

    paintStatus_ = new QLabel(toQString(paintWindowReadyStatus()), panel);
    paintStatus_->setFrameShape(QFrame::StyledPanel);
    paintStatus_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(toolbar);
    layout->addWidget(scrollArea, 1);
    layout->addWidget(paintStatus_);
    return panel;
}

QWidget* MainWindow::makeTextEditorPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    const auto textEditorStrings = textEditorText();
    auto applyTextFormat = [this](const QTextCharFormat& format) {
        if (!textEditorText_) {
            return;
        }
        textEditorText_->mergeCurrentCharFormat(format);
        if (textEditorStatus_) {
            textEditorStatus_->setText(toQString(textEditorText().localFormattingStatus));
        }
    };
    auto* boldAction = toolbar->addAction(toQString(textEditorStyleActions()[0]));
    boldAction->setToolTip(toQString(textEditorStrings.boldTooltip));
    connect(boldAction, &QAction::triggered, this, [this, applyTextFormat] {
        if (!textEditorText_) {
            return;
        }
        QTextCharFormat format;
        format.setFontWeight(textEditorText_->fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        applyTextFormat(format);
    });
    auto* italicAction = toolbar->addAction(toQString(textEditorStyleActions()[1]));
    italicAction->setToolTip(toQString(textEditorStrings.italicTooltip));
    connect(italicAction, &QAction::triggered, this, [this, applyTextFormat] {
        if (!textEditorText_) {
            return;
        }
        QTextCharFormat format;
        format.setFontItalic(!textEditorText_->fontItalic());
        applyTextFormat(format);
    });
    auto* underlineAction = toolbar->addAction(toQString(textEditorStyleActions()[2]));
    underlineAction->setToolTip(toQString(textEditorStrings.underlineTooltip));
    connect(underlineAction, &QAction::triggered, this, [this, applyTextFormat] {
        if (!textEditorText_) {
            return;
        }
        QTextCharFormat format;
        format.setFontUnderline(!textEditorText_->fontUnderline());
        applyTextFormat(format);
    });
    toolbar->addSeparator();

    auto* fontBox = new QComboBox(toolbar);
    for (const auto font : textEditorFontChoices()) {
        fontBox->addItem(toQString(font));
    }
    const QString savedTextFont =
        EditorPreferences::preferenceString(QStringLiteral("text"), QStringLiteral("font"), QStringLiteral("SansSerif"));
    const int savedTextFontIndex = fontBox->findText(savedTextFont);
    if (savedTextFontIndex >= 0) {
        fontBox->setCurrentIndex(savedTextFontIndex);
    }
    connect(fontBox, &QComboBox::currentTextChanged, this, [applyTextFormat](const QString& fontName) {
        if (fontName.isEmpty()) {
            return;
        }
        EditorPreferences::setPreferenceString(QStringLiteral("text"), QStringLiteral("font"), fontName);
        QTextCharFormat format;
        format.setFontFamilies({fontName});
        applyTextFormat(format);
    });
    toolbar->addWidget(fontBox);

    auto* sizeBox = new QComboBox(toolbar);
    for (const auto size : textEditorSizeChoices()) {
        sizeBox->addItem(toQString(size));
    }
    const QString savedTextSize =
        EditorPreferences::preferenceString(QStringLiteral("text"), QStringLiteral("size"), QStringLiteral("14"));
    const int savedTextSizeIndex = sizeBox->findText(savedTextSize);
    if (savedTextSizeIndex >= 0) {
        sizeBox->setCurrentIndex(savedTextSizeIndex);
    }
    connect(sizeBox, &QComboBox::currentTextChanged, this, [applyTextFormat](const QString& sizeText) {
        bool ok = false;
        const int size = sizeText.toInt(&ok);
        if (!ok || size <= 0) {
            return;
        }
        EditorPreferences::setPreferenceString(QStringLiteral("text"), QStringLiteral("size"), sizeText);
        QTextCharFormat format;
        format.setFontPointSize(size);
        applyTextFormat(format);
    });
    toolbar->addWidget(sizeBox);
    toolbar->addSeparator();
    const auto textActions = textEditorActionText();
    auto* applyTextAction = toolbar->addAction(toQString(textActions.applyText));
    applyTextAction->setToolTip(toQString(textActions.applyTooltip));
    connect(applyTextAction, &QAction::triggered, this, [this] { applyTextEditorChanges(); });

    bool savedSizeOk = false;
    const int savedPointSize = savedTextSize.toInt(&savedSizeOk);
    QFont textFont(savedTextFont);
    textFont.setPointSize(savedSizeOk && savedPointSize > 0 ? savedPointSize : 14);
    textEditorText_ = new QTextEdit(panel);
    textEditorText_->setFont(textFont);
    textEditorText_->setPlainText(toQString(textEditorInitialText()));
    connect(textEditorText_, &QTextEdit::textChanged, this, [this] {
        if (textEditorStatus_ && textEditorText_->document()->isModified()) {
            textEditorStatus_->setText(toQString(textEditorText().localTextStatus));
        }
    });

    textEditorStatus_ = new QLabel(toQString(textEditorReadyStatus()), panel);
    textEditorStatus_->setFrameShape(QFrame::StyledPanel);
    textEditorStatus_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(toolbar);
    layout->addWidget(textEditorText_, 1);
    layout->addWidget(textEditorStatus_);
    return panel;
}

QWidget* MainWindow::makeFieldEditorPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    const auto fieldEditorStrings = fieldEditorText();
    toolbar->addWidget(new QLabel(toQString(fieldEditorStrings.toolbarTitle), toolbar));
    toolbar->addSeparator();
    auto* wrapAction = toolbar->addAction(toQString(fieldEditorToolbarActions()[0]));
    wrapAction->setCheckable(true);
    const bool fieldWrapEnabled =
        EditorPreferences::preferenceBool(QStringLiteral("field"), QStringLiteral("wrap"), true);
    wrapAction->setChecked(fieldWrapEnabled);
    wrapAction->setToolTip(toQString(fieldEditorStrings.wrapTooltip));
    auto* scrollAction = toolbar->addAction(toQString(fieldEditorToolbarActions()[1]));
    scrollAction->setCheckable(true);
    const bool fieldScrollEnabled =
        EditorPreferences::preferenceBool(QStringLiteral("field"), QStringLiteral("scroll"), true);
    scrollAction->setChecked(fieldScrollEnabled);
    scrollAction->setToolTip(toQString(fieldEditorStrings.scrollTooltip));
    toolbar->addSeparator();
    const auto fieldActions = fieldEditorActionText();
    auto* applyFieldAction = toolbar->addAction(toQString(fieldActions.applyText));
    applyFieldAction->setToolTip(toQString(fieldActions.applyTooltip));
    connect(applyFieldAction, &QAction::triggered, this, [this] { applyFieldEditorChanges(); });

    QFont textFont(QStringLiteral("SansSerif"));
    textFont.setPointSize(14);
    fieldText_ = new QPlainTextEdit(panel);
    fieldText_->setLineWrapMode(fieldWrapEnabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    fieldText_->setVerticalScrollBarPolicy(fieldScrollEnabled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    fieldText_->setFont(textFont);
    fieldText_->setPlainText(toQString(fieldEditorInitialText()));
    connect(wrapAction, &QAction::toggled, this, [this](bool enabled) {
        EditorPreferences::setPreferenceBool(QStringLiteral("field"), QStringLiteral("wrap"), enabled);
        if (fieldText_) {
            fieldText_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        }
        if (fieldStatus_) {
            const auto fieldText = fieldEditorText();
            fieldStatus_->setText(toQString(enabled ? fieldText.wrapEnabledStatus : fieldText.wrapDisabledStatus));
        }
    });
    connect(scrollAction, &QAction::toggled, this, [this](bool enabled) {
        EditorPreferences::setPreferenceBool(QStringLiteral("field"), QStringLiteral("scroll"), enabled);
        if (fieldText_) {
            fieldText_->setVerticalScrollBarPolicy(enabled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        }
        if (fieldStatus_) {
            const auto fieldText = fieldEditorText();
            fieldStatus_->setText(toQString(enabled ? fieldText.scrollEnabledStatus : fieldText.scrollDisabledStatus));
        }
    });
    connect(fieldText_, &QPlainTextEdit::textChanged, this, [this] {
        if (fieldStatus_ && fieldText_->document()->isModified()) {
            fieldStatus_->setText(toQString(fieldEditorText().localTextStatus));
        }
    });

    fieldStatus_ = new QLabel(toQString(fieldEditorReadyStatus()), panel);
    fieldStatus_->setFrameShape(QFrame::StyledPanel);
    fieldStatus_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(toolbar);
    layout->addWidget(fieldText_, 1);
    layout->addWidget(fieldStatus_);
    return panel;
}

QWidget* MainWindow::makeVectorShapePanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);
    QAction* restoredVectorTool = nullptr;
    const QString savedVectorTool =
        EditorPreferences::preferenceString(QStringLiteral("vectorShape"), QStringLiteral("selectedTool"), QStringLiteral("Pen"));
    for (std::size_t i = 0; i < vectorShapeToolbarActions().size(); ++i) {
        if (i == 4) {
            toolbar->addSeparator();
        }
        const QString toolName = toQString(vectorShapeToolbarActions()[i]);
        auto* action = toolbar->addAction(toolName);
        action->setCheckable(true);
        action->setData(toolName);
        action->setToolTip(toQString(vectorShapeToolTipText(toolName.toStdString())));
        toolGroup->addAction(action);
        if (toolName == savedVectorTool) {
            restoredVectorTool = action;
        }
        connect(action, &QAction::triggered, this, [this, toolName] {
            EditorPreferences::setPreferenceString(QStringLiteral("vectorShape"), QStringLiteral("selectedTool"), toolName);
            const QString status = toQString(vectorShapeToolStatus(toolName.toStdString(), vectorShapeLoaded_));
            if (vectorShapeStatus_) {
                vectorShapeStatus_->setText(status);
            }
            if (statusLabel_) {
                statusLabel_->setText(status.trimmed());
            }
            if (vectorShapeLoaded_) {
                applyVectorShapeTool(toolName);
            }
        });
    }
    if (restoredVectorTool) {
        restoredVectorTool->setChecked(true);
    }

    auto* canvas = new QFrame(panel);
    canvas->setFrameShape(QFrame::StyledPanel);
    canvas->setAutoFillBackground(true);
    auto palette = canvas->palette();
    palette.setColor(QPalette::Window, Qt::white);
    canvas->setPalette(palette);
    auto* canvasLayout = new QVBoxLayout(canvas);
    vectorShapeCanvasLabel_ = new QLabel(toQString(vectorShapePlaceholderText()), canvas);
    vectorShapeCanvasLabel_->setAlignment(Qt::AlignCenter);
    vectorShapeCanvasLabel_->setWordWrap(true);
    canvasLayout->addWidget(vectorShapeCanvasLabel_, 1);

    QFont monoFont(QStringLiteral("Courier New"));
    monoFont.setStyleHint(QFont::Monospace);
    vectorShapeDetails_ = new QPlainTextEdit(panel);
    vectorShapeDetails_->setReadOnly(true);
    vectorShapeDetails_->setFont(monoFont);
    vectorShapeDetails_->setPlainText(toQString(vectorShapePlaceholderText()));
    vectorShapeDetails_->setMinimumHeight(120);

    vectorShapeStatus_ = new QLabel(toQString(vectorShapePlaceholderText()), panel);
    vectorShapeStatus_->setFrameShape(QFrame::StyledPanel);
    vectorShapeStatus_->setFrameShadow(QFrame::Sunken);

    layout->addWidget(toolbar);
    layout->addWidget(canvas, 1);
    layout->addWidget(vectorShapeDetails_);
    layout->addWidget(vectorShapeStatus_);
    return panel;
}

QWidget* MainWindow::makeSoundPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    const auto soundText = soundWindowText();

    soundPlayButton_ = new QPushButton(toQString(soundText.playButtonText), toolbar);
    soundPlayButton_->setEnabled(false);
    soundStopButton_ = new QPushButton(toQString(soundText.stopButtonText), toolbar);
    soundStopButton_->setEnabled(false);
    soundProgress_ = new QProgressBar(toolbar);
    soundProgress_->setRange(0, 100);
    soundProgress_->setValue(0);
    soundProgress_->setFixedWidth(200);
    soundTimeLabel_ = new QLabel(toQString(soundWindowInitialTimeText()), toolbar);

    toolbar->addWidget(soundPlayButton_);
    toolbar->addWidget(soundStopButton_);
    toolbar->addSeparator();
    toolbar->addWidget(soundProgress_);
    toolbar->addWidget(soundTimeLabel_);

    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(13);
    soundInfo_ = new QPlainTextEdit(panel);
    soundInfo_->setReadOnly(true);
    soundInfo_->setFont(monoFont);
    soundInfo_->setPlainText(toQString(soundWindowInitialText()));

    soundStatus_ = new QLabel(toQString(soundWindowReadyStatus()), panel);
    soundStatus_->setFrameShape(QFrame::StyledPanel);
    soundStatus_->setFrameShadow(QFrame::Sunken);

#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
    soundAudioOutput_ = new QAudioOutput(panel);
    soundAudioOutput_->setVolume(1.0);
    soundPlayer_ = new QMediaPlayer(panel);
    soundPlayer_->setAudioOutput(soundAudioOutput_);

    connect(soundPlayer_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        const double current = static_cast<double>(positionMs) / 1000.0;
        double total = soundLoadedDuration_;
        if (total <= 0.0 && soundPlayer_) {
            total = static_cast<double>(soundPlayer_->duration()) / 1000.0;
        }
        soundProgress_->setValue(total > 0.0 ? std::clamp(static_cast<int>(std::lround((current / total) * 100.0)), 0, 100)
                                             : 0);
        soundTimeLabel_->setText(toQString(soundWindowTimeText(current, total)));
    });
    connect(soundPlayer_, &QMediaPlayer::durationChanged, this, [this](qint64 durationMs) {
        if (soundLoadedDuration_ <= 0.0 && durationMs > 0) {
            soundLoadedDuration_ = static_cast<double>(durationMs) / 1000.0;
            soundTimeLabel_->setText(toQString(soundWindowTimeText(0.0, soundLoadedDuration_)));
        }
    });
    connect(soundPlayer_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        const auto soundText = soundWindowText();
        if (state == QMediaPlayer::PlayingState) {
            soundStatus_->setText(toQString(soundText.playingStatus));
        } else if (state == QMediaPlayer::StoppedState && soundPlayer_->source().isValid()) {
            soundStatus_->setText(toQString(soundText.stoppedStatus));
        }
    });
    connect(soundPlayer_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& errorString) {
        soundStatus_->setText(toQString(soundWindowPlaybackErrorStatusText(errorString.toStdString())));
    });

    connect(soundPlayButton_, &QPushButton::clicked, this, [this] {
        if (soundPlayer_ && soundPlayer_->source().isValid()) {
            soundPlayer_->play();
        } else {
            soundStatus_->setText(toQString(soundWindowPlaybackUnavailableStatus()));
        }
    });
    connect(soundStopButton_, &QPushButton::clicked, this, [this] {
        if (soundPlayer_) {
            soundPlayer_->stop();
        }
        soundProgress_->setValue(0);
        soundTimeLabel_->setText(toQString(soundWindowTimeText(0.0, soundLoadedDuration_)));
        soundStatus_->setText(toQString(soundWindowText().stoppedStatus));
    });
#else
    connect(soundPlayButton_, &QPushButton::clicked, this, [this] {
        soundStatus_->setText(toQString(soundWindowPlaybackUnavailableStatus()));
    });
    connect(soundStopButton_, &QPushButton::clicked, this, [this] {
        soundProgress_->setValue(0);
        soundTimeLabel_->setText(toQString(soundWindowInitialTimeText()));
        soundStatus_->setText(toQString(soundWindowText().stoppedStatus));
    });
#endif

    layout->addWidget(toolbar);
    layout->addWidget(soundInfo_, 1);
    layout->addWidget(soundStatus_);
    return panel;
}

QWidget* MainWindow::makeColorPalettesPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    auto* selectorRow = new QWidget(panel);
    auto* selectorLayout = new QHBoxLayout(selectorRow);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    const auto paletteText = colorPaletteWindowText();
    selectorLayout->addWidget(new QLabel(toQString(paletteText.selectorLabel), selectorRow));

    auto* selector = new QComboBox(selectorRow);
    for (const auto paletteName : colorPaletteChoices()) {
        selector->addItem(toQString(paletteName));
    }
    const QString savedPalette =
        EditorPreferences::preferenceString(QStringLiteral("colorPalettes"), QStringLiteral("selectedPalette"), {});
    const int savedPaletteIndex = selector->findText(savedPalette);
    if (savedPaletteIndex >= 0) {
        selector->setCurrentIndex(savedPaletteIndex);
    }
    selectorLayout->addWidget(selector);
    selectorLayout->addStretch(1);

    auto* preview = new QFrame(panel);
    preview->setFrameShape(QFrame::StyledPanel);
    preview->setAutoFillBackground(true);
    auto palette = preview->palette();
    palette.setColor(QPalette::Window, Qt::white);
    preview->setPalette(palette);

    auto* previewLayout = new QVBoxLayout(preview);
    colorPalettePreviewLabel_ = new QLabel(toQString(colorPalettePlaceholderText()), preview);
    colorPalettePreviewLabel_->setAlignment(Qt::AlignCenter);
    colorPalettePreviewLabel_->setWordWrap(true);
    previewLayout->addWidget(colorPalettePreviewLabel_, 1);

    QFont monoFont(QStringLiteral("Courier New"));
    monoFont.setStyleHint(QFont::Monospace);
    colorPaletteInfo_ = new QPlainTextEdit(panel);
    colorPaletteInfo_->setReadOnly(true);
    colorPaletteInfo_->setFont(monoFont);
    colorPaletteInfo_->setPlainText(toQString(colorPalettePlaceholderText()));
    colorPaletteInfo_->setMinimumHeight(120);

    auto renderPaletteChoice = [this](const QString& value) {
        statusLabel_->setText(toQString(colorPaletteStatusText(value.toStdString())));
        if (!colorPalettePreviewLabel_) {
            return;
        }
        colorPalettePreviewLabel_->setPixmap({});
        if (colorPaletteInfo_) {
            colorPaletteInfo_->setPlainText(toQString(colorPalettePlaceholderText()));
        }
        const auto symbol = colorPaletteBuiltInSymbol(value.toStdString());
        const auto* palette = symbol ? bitmap::Palette::builtInBySymbolName(*symbol) : nullptr;
        if (palette != nullptr) {
            const auto swatch = buildPaletteSwatch(palette->colors());
            const auto details = toQString(previewPaletteInfo(palette->colors()));
            colorPalettePreviewLabel_->setText({});
            colorPalettePreviewLabel_->setPixmap(paletteSwatchPixmap(swatch));
            colorPalettePreviewLabel_->setMinimumSize(swatch.width, swatch.height);
            colorPalettePreviewLabel_->setToolTip(details);
            if (colorPaletteInfo_) {
                colorPaletteInfo_->setPlainText(details);
            }
            statusLabel_->setText(toQString(colorPaletteLoadedStatusText(value.toStdString(), palette->size())));
        } else {
            const auto unavailableText = toQString(colorPaletteUnavailableText(value.toStdString()));
            colorPalettePreviewLabel_->setText(unavailableText);
            colorPalettePreviewLabel_->setMinimumSize(0, 0);
            colorPalettePreviewLabel_->setToolTip(QString());
            if (colorPaletteInfo_) {
                colorPaletteInfo_->setPlainText(unavailableText);
            }
        }
    };
    connect(selector, &QComboBox::currentTextChanged, this, [renderPaletteChoice](const QString& value) {
        EditorPreferences::setPreferenceString(QStringLiteral("colorPalettes"), QStringLiteral("selectedPalette"), value);
        renderPaletteChoice(value);
    });
    QMetaObject::invokeMethod(selector, [selector, renderPaletteChoice] {
        renderPaletteChoice(selector->currentText());
    }, Qt::QueuedConnection);

    layout->addWidget(selectorRow);
    layout->addWidget(preview, 1);
    layout->addWidget(colorPaletteInfo_);
    return panel;
}

QWidget* MainWindow::makeBytecodeDebuggerPanel() {
    auto* panel = new QWidget;
    auto* root = new QVBoxLayout(panel);

    auto* toolbar = new QToolBar(panel);
    toolbar->setMovable(false);
    const auto debugCommands = debugCommandSpecs();
    int toolbarActionCount = 0;
    for (const auto& command : debugCommands) {
        if (command.toolbarText.empty()) {
            continue;
        }
        if (toolbarActionCount == 3 || toolbarActionCount == 4) {
            toolbar->addSeparator();
        }
        auto* action = toolbar->addAction(toQString(command.toolbarText));
        if (!command.shortcut.empty()) {
            action->setToolTip(toQString(debugToolbarToolTipText(command.toolbarText, command.shortcut)));
        }
        action->setEnabled(true);
        connect(action, &QAction::triggered, this, [this, command] { triggerDebugCommand(command.id); });
        ++toolbarActionCount;
    }

    auto* statusBlock = new QWidget(panel);
    auto* statusLayout = new QVBoxLayout(statusBlock);
    statusLayout->setContentsMargins(4, 4, 4, 4);
    debugStatusLabel_ = new QLabel(toQString(debugInitialStatusText()), statusBlock);
    debugStatusLabel_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(11);
    debugHandlerLabel_ = new QLabel(toQString(debugInitialHandlerText()), statusBlock);
    debugHandlerLabel_->setFont(monoFont);
    statusLayout->addWidget(debugStatusLabel_);
    statusLayout->addWidget(debugHandlerLabel_);

    auto* splitter = new QSplitter(Qt::Vertical, panel);

    auto* topPanel = new QWidget(splitter);
    auto* topLayout = new QVBoxLayout(topPanel);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto* scriptBrowser = new QWidget(topPanel);
    auto* browserLayout = new QVBoxLayout(scriptBrowser);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    const auto debugBrowser = debugBrowserText();

    auto* scriptRow = new QWidget(scriptBrowser);
    auto* scriptLayout = new QHBoxLayout(scriptRow);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    scriptLayout->addWidget(new QLabel(toQString(debugBrowser.scriptLabel), scriptRow));
    debugScriptFilter_ = new QLineEdit(scriptRow);
    debugScriptFilter_->setToolTip(toQString(debugBrowser.scriptFilterTooltip));
    debugScriptFilter_->setPlaceholderText(toQString(debugBrowser.filterPlaceholder));
    debugScriptFilter_->setText(
        EditorPreferences::preferenceString(QStringLiteral("bytecodeDebugger"), QStringLiteral("scriptFilter"), {}));
    debugScriptSelector_ = new QComboBox(scriptRow);
    debugScriptSelector_->setMinimumWidth(240);
    scriptLayout->addWidget(debugScriptFilter_);
    scriptLayout->addWidget(debugScriptSelector_, 1);

    auto* handlerRow = new QWidget(scriptBrowser);
    auto* handlerLayout = new QHBoxLayout(handlerRow);
    handlerLayout->setContentsMargins(0, 0, 0, 0);
    handlerLayout->addWidget(new QLabel(toQString(debugBrowser.handlerLabel), handlerRow));
    debugHandlerFilter_ = new QLineEdit(handlerRow);
    debugHandlerFilter_->setToolTip(toQString(debugBrowser.handlerFilterTooltip));
    debugHandlerFilter_->setPlaceholderText(toQString(debugBrowser.filterPlaceholder));
    debugHandlerFilter_->setText(
        EditorPreferences::preferenceString(QStringLiteral("bytecodeDebugger"), QStringLiteral("handlerFilter"), {}));
    debugHandlerSelector_ = new QComboBox(handlerRow);
    debugHandlerSelector_->setMinimumWidth(180);
    debugHandlerDetailsButton_ = new QPushButton(toQString(debugBrowser.detailsButtonText), handlerRow);
    debugHandlerDetailsButton_->setToolTip(toQString(debugBrowser.detailsButtonTooltip));
    debugHandlerDetailsButton_->setEnabled(false);
    handlerLayout->addWidget(debugHandlerFilter_);
    handlerLayout->addWidget(debugHandlerSelector_, 1);
    handlerLayout->addWidget(debugHandlerDetailsButton_);

    browserLayout->addWidget(scriptRow);
    browserLayout->addWidget(handlerRow);

    auto* bytecodeGroup = new QGroupBox(toQString(debugBrowser.bytecodeTitle), topPanel);
    auto* bytecodeLayout = new QVBoxLayout(bytecodeGroup);
    debugBytecodeList_ = new QListWidget(bytecodeGroup);
    debugBytecodeList_->setFont(monoFont);
    debugBytecodeList_->setSelectionMode(QAbstractItemView::SingleSelection);
    debugBytecodeList_->setUniformItemSizes(false);
    debugBytecodeList_->setWordWrap(false);
    debugBytecodeList_->setContextMenuPolicy(Qt::CustomContextMenu);
    setDebugBytecodeListing(toQString(debugBytecodePlaceholderText()));
    connect(debugBytecodeList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showBytecodeContextMenu(pos);
    });
    connect(debugBytecodeList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        debugBytecodeList_->setCurrentItem(item);
        const QString target = selectedDebugCallTargetName();
        if (!target.isEmpty() && debugHandlerExists(target)) {
            navigateToDebugHandler(target, false);
        }
    });
    connect(debugBytecodeList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        debugBytecodeList_->setCurrentItem(item);
        toggleSelectedBytecodeBreakpoint();
    });
    auto* bytecodeFooter = new QWidget(bytecodeGroup);
    auto* footerLayout = new QHBoxLayout(bytecodeFooter);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    auto* legend = new QLabel(toQString(debugBytecodeLegendText()), bytecodeFooter);
    legend->setWordWrap(true);
    debugLingoToggle_ = new QPushButton(toQString(debugBrowser.lingoToggleText), bytecodeFooter);
    debugLingoToggle_->setCheckable(true);
    const QString debugDefaultView =
        EditorPreferences::preferenceString(QStringLiteral("script"), QStringLiteral("defaultView"), QStringLiteral("Lingo"));
    debugLingoToggle_->setChecked(debugDefaultView != QStringLiteral("Bytecode"));
    debugLingoToggle_->setEnabled(false);
    debugLingoToggle_->setToolTip(toQString(debugBrowser.lingoToggleTooltip));
    connect(debugLingoToggle_, &QPushButton::toggled, this, [this](bool lingo) {
        EditorPreferences::setPreferenceString(QStringLiteral("script"),
                                               QStringLiteral("defaultView"),
                                               lingo ? QStringLiteral("Lingo") : QStringLiteral("Bytecode"));
        updateBytecodeDebuggerPreview();
    });
    footerLayout->addWidget(legend, 1);
    footerLayout->addWidget(debugLingoToggle_);
    bytecodeLayout->addWidget(debugBytecodeList_, 1);
    bytecodeLayout->addWidget(bytecodeFooter);

    connect(debugScriptSelector_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateBytecodeDebuggerPreview();
    });
    connect(debugScriptFilter_, &QLineEdit::textChanged, this, [this](const QString& value) {
        EditorPreferences::setPreferenceString(QStringLiteral("bytecodeDebugger"), QStringLiteral("scriptFilter"), value);
        refreshBytecodeDebuggerScriptFilter();
    });
    connect(debugHandlerFilter_, &QLineEdit::textChanged, this, [this](const QString& value) {
        EditorPreferences::setPreferenceString(QStringLiteral("bytecodeDebugger"), QStringLiteral("handlerFilter"), value);
        updateBytecodeDebuggerPreview();
    });
    connect(debugHandlerDetailsButton_, &QPushButton::clicked, this, [this] {
        showBytecodeHandlerDetails();
    });
    connect(debugHandlerSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!debugBytecodeList_ || !debugScriptSelector_) {
            return;
        }
        const auto handlerPreviews = debugScriptSelector_->currentData().toStringList();
        const QString handlerFilter = debugHandlerFilter_ ? debugHandlerFilter_->text().trimmed() : QString{};
        QStringList filteredHandlerPreviews;
        for (const auto& handlerPreview : handlerPreviews) {
            const QString handlerName = handlerPreview.section(QLatin1Char('\n'), 0, 0);
            if (handlerFilter.isEmpty() || handlerName.contains(handlerFilter, Qt::CaseInsensitive)) {
                filteredHandlerPreviews.push_back(handlerPreview);
            }
        }
        if (filteredHandlerPreviews.isEmpty()) {
            setDebugBytecodeListing(toQString(debugBytecodePlaceholderText()));
            if (debugHandlerDetailsButton_) {
                debugHandlerDetailsButton_->setEnabled(false);
            }
            if (debugHandlerLabel_) {
                debugHandlerLabel_->setText(toQString(debugInitialHandlerText()));
            }
        } else if (index <= 0) {
            const int scriptId = currentDebugScriptId();
            QStringList decoratedPreviews;
            for (const auto& handlerPreview : filteredHandlerPreviews) {
                decoratedPreviews.push_back(decorateDebugHandlerPreview(handlerPreview,
                                                                        scriptId,
                                                                        handlerPreview.section(QLatin1Char('\n'), 0, 0)));
            }
            setDebugBytecodeListing(decoratedPreviews.join(QStringLiteral("\n")));
            if (debugHandlerDetailsButton_) {
                debugHandlerDetailsButton_->setEnabled(false);
            }
            if (debugHandlerLabel_) {
                debugHandlerLabel_->setText(debugHandlerSelector_->currentText());
            }
        } else if (index - 1 < filteredHandlerPreviews.size()) {
            const auto handlerPreview = filteredHandlerPreviews.at(index - 1);
            setDebugBytecodeListing(decorateDebugHandlerPreview(handlerPreview,
                                                                currentDebugScriptId(),
                                                                handlerPreview.section(QLatin1Char('\n'), 0, 0)));
            if (debugHandlerDetailsButton_) {
                debugHandlerDetailsButton_->setEnabled(true);
            }
            if (debugHandlerLabel_) {
                debugHandlerLabel_->setText(debugHandlerSelector_->currentText());
            }
        }
    });

    topLayout->addWidget(scriptBrowser);
    topLayout->addWidget(bytecodeGroup, 1);

    auto* stateTabs = new QTabWidget(splitter);
    debugStackTable_ = makeReadOnlyTable(debugStackTableColumns(), stateTabs);
    debugLocalsTable_ = makeReadOnlyTable(debugVariableTableColumns(), stateTabs);
    debugGlobalsTable_ = makeReadOnlyTable(debugVariableTableColumns(), stateTabs);
    connect(debugStackTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugStackTable_, row, column);
    });
    connect(debugLocalsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugLocalsTable_, row, column);
    });
    connect(debugGlobalsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugGlobalsTable_, row, column);
    });
    stateTabs->addTab(debugStackTable_, toQString(debugStateTabNames()[0]));
    stateTabs->addTab(debugLocalsTable_, toQString(debugStateTabNames()[1]));
    stateTabs->addTab(debugGlobalsTable_, toQString(debugStateTabNames()[2]));

    auto* watchesPanel = new QWidget(stateTabs);
    auto* watchesLayout = new QVBoxLayout(watchesPanel);
    debugWatchesTable_ = makeReadOnlyTable(debugWatchTableColumns(), watchesPanel);
    connect(debugWatchesTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelectedDebugWatch(); });
    watchesLayout->addWidget(debugWatchesTable_, 1);
    auto* watchesButtons = new QWidget(watchesPanel);
    auto* watchesButtonLayout = new QHBoxLayout(watchesButtons);
    watchesButtonLayout->setContentsMargins(0, 0, 0, 0);
    for (const auto& actionSpec : debugWatchActionSpecs()) {
        auto* button = new QPushButton(toQString(actionSpec.buttonText), watchesButtons);
        button->setToolTip(toQString(actionSpec.tooltipText));
        const auto id = actionSpec.id;
        if (id == "add-watch") {
            connect(button, &QPushButton::clicked, this, [this] { addDebugWatch(); });
        } else if (id == "remove-watch") {
            connect(button, &QPushButton::clicked, this, [this] { removeSelectedDebugWatch(); });
        } else if (id == "edit-watch") {
            connect(button, &QPushButton::clicked, this, [this] { editSelectedDebugWatch(); });
        } else if (id == "clear-watches") {
            connect(button, &QPushButton::clicked, this, [this] { clearDebugWatches(); });
        }
        watchesButtonLayout->addWidget(button);
    }
    watchesButtonLayout->addStretch(1);
    watchesLayout->addWidget(watchesButtons);
    stateTabs->addTab(watchesPanel, toQString(debugStateTabNames()[3]));

    auto* objectsPanel = new QWidget(stateTabs);
    auto* objectsLayout = new QVBoxLayout(objectsPanel);
    auto addObjectSection = [&](std::string_view title, QTableWidget* table) {
        auto* group = new QGroupBox(toQString(title), objectsPanel);
        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->addWidget(table);
        objectsLayout->addWidget(group);
    };
    debugTimeoutsTable_ = makeReadOnlyTable(debugTimeoutTableColumns(), objectsPanel);
    debugObjectGlobalsTable_ = makeReadOnlyTable(debugVariableTableColumns(), objectsPanel);
    debugMoviePropertiesTable_ = makeReadOnlyTable(debugMoviePropertyTableColumns(), objectsPanel);
    connect(debugTimeoutsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugTimeoutsTable_, row, column);
    });
    connect(debugObjectGlobalsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugObjectGlobalsTable_, row, column);
    });
    connect(debugMoviePropertiesTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        showDebugDatumDetails(debugMoviePropertiesTable_, row, column);
    });
    addObjectSection(debugObjectSectionNames()[0], debugTimeoutsTable_);
    addObjectSection(debugObjectSectionNames()[1], debugObjectGlobalsTable_);
    addObjectSection(debugObjectSectionNames()[2], debugMoviePropertiesTable_);
    stateTabs->addTab(objectsPanel, toQString(debugStateTabNames()[4]));

    splitter->addWidget(topPanel);
    splitter->addWidget(stateTabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    root->addWidget(toolbar);
    root->addWidget(statusBlock);
    root->addWidget(splitter, 1);
    return panel;
}

} // namespace libreshockwave::editor
