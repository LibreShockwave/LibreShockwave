#include "stage_widget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include "libreshockwave/player/input/director_key_codes.hpp"

namespace libreshockwave::debugger {
namespace {

constexpr QSize k_default_stage_size(320, 240);

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
    setMinimumSize(k_default_stage_size);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    stage_surface_ = new StageSurface(this);
    stage_surface_->hide();
}

void StageWidget::prepareForMovie() {
    stage_size_ = QSize();
    stage_surface_->hide();
    setMinimumSize(k_default_stage_size);
    updateGeometry();
    update();
}

void StageWidget::setFrameImage(const QImage& image) {
    if (image.isNull() || !image.size().isValid()) {
        stage_size_ = QSize();
        stage_surface_->hide();
        update();
        return;
    }

    stage_size_ = image.size();
    stage_surface_->resize(stage_size_);
    static_cast<StageSurface*>(stage_surface_)->setImage(image);
    positionStageSurface();
    stage_surface_->show();
    update();
}

void StageWidget::setInputCallback(InputCallback callback) {
    input_callback_ = std::move(callback);
}

void StageWidget::clearFrame() {
    stage_size_ = QSize();
    stage_surface_->hide();
    update();
}

QSize StageWidget::sizeHint() const {
    return k_default_stage_size;
}

void StageWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (stage_size_.isEmpty()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No movie loaded"));
    }
}

void StageWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionStageSurface();
}

void StageWidget::widgetToStage(int wx, int wy, int& sx, int& sy) const {
    if (stage_size_.isEmpty()) {
        sx = 0;
        sy = 0;
        return;
    }

    const int ox = stage_surface_->x();
    const int oy = stage_surface_->y();
    sx = wx - ox;
    sy = wy - oy;

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= stage_size_.width()) sx = stage_size_.width() - 1;
    if (sy >= stage_size_.height()) sy = stage_size_.height() - 1;
}

void StageWidget::positionStageSurface() {
    if (stage_surface_ == nullptr || stage_size_.isEmpty()) {
        return;
    }

    stage_surface_->move((width() - stage_size_.width()) / 2,
                        (height() - stage_size_.height()) / 2);
}

// Private code base for printable characters with no DOM equivalent (shifted
// punctuation such as ! @ # % < >). The offset keeps them clear of every
// special browser/Director code so DirectorKeyCodes passes them through.
constexpr int k_synthetic_printable_code_base = 0x2000;

// Helper: map Qt key to a simple DOM-like keyCode (the WASM bridge pattern)
static int qtKeyToBrowserKeyCode(int qt_key) {
    if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) return qt_key;
    if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) return qt_key;
    if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F12) return 112 + (qt_key - Qt::Key_F1);
    // On Linux, Shift+digit arrives as the shifted character's own key
    // (Qt::Key_Exclam etc.), not Key_1; those Latin-1 printables need this path.
    if (qt_key > Qt::Key_Space && qt_key <= Qt::Key_ydiaeresis) {
        return k_synthetic_printable_code_base + qt_key;
    }
    switch (qt_key) {
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
static int qtKeyToDirectorKeyCode(int qt_key) {
    return libreshockwave::player::input::DirectorKeyCodes::fromBrowserKeyCode(
        qtKeyToBrowserKeyCode(qt_key));
}

void StageWidget::mouseMoveEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    if (input_callback_) {
        input_callback_(0 /*MouseMove*/, sx, sy, 0, "", false, false, false, false);
    }
}

void StageWidget::mousePressEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    const bool right_button = (event->button() == Qt::RightButton);
    if (input_callback_) {
        input_callback_(1 /*MouseDown*/, sx, sy, 0, "",
                       (event->modifiers() & Qt::ShiftModifier) != 0,
                       (event->modifiers() & Qt::ControlModifier) != 0,
                       (event->modifiers() & Qt::AltModifier) != 0,
                       right_button);
    }
}

void StageWidget::mouseReleaseEvent(QMouseEvent* event) {
    int sx = 0, sy = 0;
    widgetToStage(static_cast<int>(event->position().x()),
                  static_cast<int>(event->position().y()), sx, sy);
    const bool right_button = (event->button() == Qt::RightButton);
    if (input_callback_) {
        input_callback_(2 /*MouseUp*/, sx, sy, 0, "",
                       (event->modifiers() & Qt::ShiftModifier) != 0,
                       (event->modifiers() & Qt::ControlModifier) != 0,
                       (event->modifiers() & Qt::AltModifier) != 0,
                       right_button);
    }
}

void StageWidget::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (qtKeyToBrowserKeyCode(event->key()) == 0 || !input_callback_) {
        QWidget::keyPressEvent(event);
        return;
    }
    input_callback_(3 /*KeyDown*/, 0, 0, qtKeyToDirectorKeyCode(event->key()),
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
    if (qtKeyToBrowserKeyCode(event->key()) == 0 || !input_callback_) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    input_callback_(4 /*KeyUp*/, 0, 0, qtKeyToDirectorKeyCode(event->key()),
                   "",
                   (event->modifiers() & Qt::ShiftModifier) != 0,
                   (event->modifiers() & Qt::ControlModifier) != 0,
                   (event->modifiers() & Qt::AltModifier) != 0,
                   false);
}

} // namespace libreshockwave::debugger
