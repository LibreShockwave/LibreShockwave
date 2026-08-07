#include "StageWidget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace libreshockwave::debugger {

StageWidget::StageWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void StageWidget::setFrameImage(const QImage& image) {
    currentFrame_ = image;
    update();
}

void StageWidget::setInputCallback(InputCallback callback) {
    inputCallback_ = std::move(callback);
}

void StageWidget::clearFrame() {
    currentFrame_ = QImage();
    update();
}

void StageWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (currentFrame_.isNull()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No movie loaded"));
        return;
    }

    // Scale the frame to fit, preserving aspect ratio, with pixel-art (nearest-neighbor) scaling
    const auto targetRect = currentFrame_.scaled(
        size(), Qt::KeepAspectRatio, Qt::FastTransformation);

    const int x = (width() - targetRect.width()) / 2;
    const int y = (height() - targetRect.height()) / 2;

    painter.drawImage(x, y, currentFrame_.scaled(
        targetRect.width(), targetRect.height(),
        Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

void StageWidget::widgetToStage(int wx, int wy, int& sx, int& sy) const {
    if (currentFrame_.isNull()) {
        sx = 0;
        sy = 0;
        return;
    }

    const auto targetSize = currentFrame_.size().scaled(size(), Qt::KeepAspectRatio);
    const int ox = (width() - targetSize.width()) / 2;
    const int oy = (height() - targetSize.height()) / 2;

    const float scaleX = static_cast<float>(currentFrame_.width()) / targetSize.width();
    const float scaleY = static_cast<float>(currentFrame_.height()) / targetSize.height();

    sx = static_cast<int>((wx - ox) * scaleX);
    sy = static_cast<int>((wy - oy) * scaleY);

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= currentFrame_.width()) sx = currentFrame_.width() - 1;
    if (sy >= currentFrame_.height()) sy = currentFrame_.height() - 1;
}

// Helper: map Qt key to a simple DOM-like keyCode (the WASM bridge pattern)
static int qtKeyToBrowserKeyCode(int qtKey) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) return qtKey;
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) return qtKey;
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) return 112 + (qtKey - Qt::Key_F1);
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
    const int keyCode = qtKeyToBrowserKeyCode(event->key());
    if (keyCode == 0 || !inputCallback_) {
        QWidget::keyPressEvent(event);
        return;
    }
    inputCallback_(3 /*KeyDown*/, 0, 0, keyCode, event->text().toStdString(),
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
    const int keyCode = qtKeyToBrowserKeyCode(event->key());
    if (keyCode == 0 || !inputCallback_) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    inputCallback_(4 /*KeyUp*/, 0, 0, keyCode, "",
                   (event->modifiers() & Qt::ShiftModifier) != 0,
                   (event->modifiers() & Qt::ControlModifier) != 0,
                   (event->modifiers() & Qt::AltModifier) != 0,
                   false);
}

} // namespace libreshockwave::debugger
