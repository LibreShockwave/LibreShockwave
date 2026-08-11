#pragma once

#include <QLabel>
#include <QPlainTextEdit>
#include <QWidget>

class QCheckBox;
class QPushButton;

namespace libreshockwave::debugger {

/// Panel reporting whether debug mode is enabled, with a toggle switch and a
/// debug print log (put/alert/debug output and VM trace lines).
class DebugWindowPanel : public QWidget {
    Q_OBJECT

public:
    explicit DebugWindowPanel(QWidget* parent = nullptr);

    /// Update the reported debug mode state.
    void setDebugEnabled(bool enabled);

    /// True while the debug toggle is on.
    [[nodiscard]] bool debugEnabled() const;

public slots:
    /// Append a tagged line to the debug log. Dropped while debug is off.
    void appendLogLine(const QString& tag, const QString& text);

    /// Clear the debug log.
    void clearLog();

signals:
    /// Emitted when the user flips the debug toggle.
    void debugEnabledChanged(bool enabled);

private:
    QCheckBox* toggle_;
    QLabel* stateLabel_;
    QPlainTextEdit* logView_;
    QPushButton* clearButton_;
    bool enabled_{true};
};

} // namespace libreshockwave::debugger
