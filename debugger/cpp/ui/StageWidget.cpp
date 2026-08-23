#include "StageWidget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include "libreshockwave/player/input/DirectorKeyCodes.hpp"

namespace libreshockwave::debugger {
namespace {

constexpr QSize kDefaultStageSize(320, 240);

class StageSurface final : public QWidget {
public:
    explicit StageSurface(QWidget* parent)
        : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setImage(const QImage& image) {
        image_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.drawImage(QPoint(0, 0), image_);
    }

private:
    QImage image_;
};

} // namespace

StageWidget::StageWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(kDefaultStageSize);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    stageSurface_ = new StageSurface(this);
    stageSurface_->hide();
}

void StageWidget::prepareForMovie() {
    stageSize_ = QSize();
    stageSurface_->hide();
    setMinimumSize(kDefaultStageSize);
    updateGeometry();
    update();
}

void StageWidget::setFrameImage(const QImage& image) {
    if (image.isNull() || !image.size().isValid()) {
        stageSize_ = QSize();
        stageSurface_->hide();
        update();
        return;
    }

    stageSize_ = image.size();
    stageSurface_->resize(stageSize_);
    static_cast<StageSurface*>(stageSurface_)->setImage(image);
    positionStageSurface();
    stageSurface_->show();
    update();
}

void StageWidget::setInputCallback(InputCallback callback) {
    inputCallback_ = std::move(callback);
}

void StageWidget::clearFrame() {
    stageSize_ = QSize();
    stageSurface_->hide();
    update();
}

QSize StageWidget::sizeHint() const {
    return kDefaultStageSize;
}

void StageWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (stageSize_.isEmpty()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No movie loaded"));
    }
}

void StageWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionStageSurface();
}

void StageWidget::widgetToStage(int wx, int wy, int& sx, int& sy) const {
    if (stageSize_.isEmpty()) {
        sx = 0;
        sy = 0;
        return;
    }

    const int ox = stageSurface_->x();
    const int oy = stageSurface_->y();
    sx = wx - ox;
    sy = wy - oy;

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= stageSize_.width()) sx = stageSize_.width() - 1;
    if (sy >= stageSize_.height()) sy = stageSize_.height() - 1;
}

void StageWidget::positionStageSurface() {
    if (stageSurface_ == nullptr || stageSize_.isEmpty()) {
        return;
    }

    stageSurface_->move((width() - stageSize_.width()) / 2,
                        (height() - stageSize_.height()) / 2);
}

// Private code base for printable characters with no DOM equivalent (shifted
// punctuation such as ! @ # % < >). The offset keeps them clear of every
// special browser/Director code so DirectorKeyCodes passes them through.
constexpr int kSyntheticPrintableCodeBase = 0x2000;

// Helper: map Qt key to a simple DOM-like keyCode (the WASM bridge pattern)
static int qtKeyToBrowserKeyCode(int qtKey) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) return qtKey;
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) return qtKey;
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) return 112 + (qtKey - Qt::Key_F1);
    // On Linux, Shift+digit arrives as the shifted character's own key
    // (Qt::Key_Exclam etc.), not Key_1; those Latin-1 printables need this path.
    if (qtKey > Qt::Key_Space && qtKey <= Qt::Key_ydiaeresis) {
        return kSyntheticPrintableCodeBase + qtKey;
    }
    switch (qtKey) {
        case Qt::Key_Return:    return 13;
        case Qt::Key_Enter:     return 13;
        case Qt::Key_Tab:       return 9;
        case Qt::Key_Backspace: return 8;
        case Qt::Key_Delete:    return 46;
        case Qt::Key_Insert:    return 45;
        case Qt::Key_Home:      return 36;
        case Qt::Key_End:       return 35;
        case Qt::Key_PageUp:    return 33;
        case Qt::Key_PageDown:  return 34;
        case Qt::Key_Left:      return 37;
        case Qt::Key_Up:        return 38;
        case Qt::Key_Right:     return 39;
        case Qt::Key_Down:      return 40;
        case Qt::Key_Escape:    return 27;
        case Qt::Key_Space:     return 32;
        case Qt::Key_Shift:     return 16;
        case Qt::Key_Control:   return 17;
        case Qt::Key_Alt:       return 18;
        case Qt::Key_CapsLock:  return 20;
        case Qt::Key_Pause:     return 19;
        default:                return 0;
    }
}

// Map a Qt key to the Director key code the player InputHandler expects, via
// the same DirectorKeyCodes conversion the WASM bridge applies. Returns 0 only
// for unmapped Qt keys; Director uses 0 for valid keys (letter A), so callers
// must not treat 0 as "unknown" here.
static int qtKeyToDirectorKeyCode(int qtKey) {
    return libreshockwave::player::input::DirectorKeyCodes::fromBrowserKeyCode(
        qtKeyToBrowserKeyCode(qtKey));
}

void StageWidget::mouseMoveEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    if (inputCallback_) {
        inputCallback_(0 /*MouseMove*/, sx, sy, 0, "", false, false, false, false);
    }
}

void StageWidget::mousePressEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    const bool rightButton = (event->button() == Qt::RightButton);
    if (inputCallback_) {
        inputCallback_(1 /*MouseDown*/, sx, sy, 0, "",
                       (event->modifiers() & Qt::ShiftModifier) != 0,
                       (event->modifiers() & Qt::ControlModifier) != 0,
                       (event->modifiers() & Qt::AltModifier) != 0,
                       rightButton);
    }
}

void StageWidget::mouseReleaseEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    const bool rightButton = (event->button() == Qt::RightButton);
    if (inputCallback_) {
        inputCallback_(2 /*MouseUp*/, sx, sy, 0, "",
                       (event->modifiers() & Qt::ShiftModifier) != 0,
                       (event->modifiers() & Qt::ControlModifier) != 0,
                       (event->modifiers() & Qt::AltModifier) != 0,
                       rightButton);
    }
}

void StageWidget::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (qtKeyToBrowserKeyCode(event->key()) == 0 || !inputCallback_) {
        QWidget::keyPressEvent(event);
        return;
    }
    inputCallback_(3 /*KeyDown*/, 0, 0, qtKeyToDirectorKeyCode(event->key()),
                   event->text().toStdString(),
                   (event->modifiers() & Qt::ShiftModifier) != 0,
                   (event->modifiers() & Qt::ControlModifier) != 0,
                   (event->modifiers() & Qt::AltModifier) != 0,
                   false);
}

void StageWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    if (qtKeyToBrowserKeyCode(event->key()) == 0 || !inputCallback_) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    inputCallback_(4 /*KeyUp*/, 0, 0, qtKeyToDirectorKeyCode(event->key()),
                   "",
                   (event->modifiers() & Qt::ShiftModifier) != 0,
                   (event->modifiers() & Qt::ControlModifier) != 0,
                   (event->modifiers() & Qt::AltModifier) != 0,
                   false);
}

} // namespace libreshockwave::debugger
