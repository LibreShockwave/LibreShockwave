#pragma once

#include <QImage>
#include <QWidget>
#include <functional>

namespace libreshockwave::debugger {

/// Widget that renders the movie's stage bitmap with pixel-art scaling.
/// Forwards keyboard and mouse input to a callback (typically enqueuing into
/// DebuggerContext's input queue).
class StageWidget : public QWidget {
    Q_OBJECT

public:
    /// Callback type for input events.
    /// (stageX, stageY, keyCode, keyText, shift, ctrl, alt, isKeyDown)
    using InputCallback = std::function<void(int type, int stage_x, int stage_y,
                                             int key_code, const std::string& key_text,
                                             bool shift, bool ctrl, bool alt,
                                             bool right_button)>;

    explicit StageWidget(QWidget* parent = nullptr);

    /// Reset the stage viewport before installing a newly loaded movie.
    void prepareForMovie();

    /// Set the current frame image to display.
    void setFrameImage(const QImage& image);

    /// Set the callback for input events.
    void setInputCallback(InputCallback callback);

    /// Clear the current frame (show blank).
    void clearFrame();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    QSize sizeHint() const override;

private:
    /// Map widget coordinates to stage coordinates.
    void widgetToStage(int wx, int wy, int& sx, int& sy) const;
    void positionStageSurface();

    QWidget* stage_surface_{nullptr};
    QSize stage_size_;
    InputCallback input_callback_;
};

} // namespace libreshockwave::debugger
