#pragma once

#include <QObject>

#include "libreshockwave/player/debug/DebugStateListener.hpp"
#include "model/DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Bridges DebugStateListener callbacks (from the VM worker thread) to Qt
/// signals delivered on the main thread via queued connections.
class DebugStateBridge : public QObject,
                         public libreshockwave::player::debug::DebugStateListener {
    Q_OBJECT

public:
    explicit DebugStateBridge(QObject* parent = nullptr);
    ~DebugStateBridge() override = default;

    // DebugStateListener implementation — called from the VM worker thread.
    void onPaused(const libreshockwave::player::debug::DebugSnapshot& snapshot) override;
    void onResumed() override;
    void onBreakpointsChanged() override;
    void onWatchExpressionsChanged() override;

signals:
    /// Emitted when execution pauses at a breakpoint or step.
    void paused(const SnapshotData& data);

    /// Emitted when execution resumes after being paused.
    void resumed();

    /// Emitted when breakpoints are added, removed, or toggled.
    void breakpointsChanged();

    /// Emitted when watch expressions are added, removed, or modified.
    void watchExpressionsChanged();

private:
    [[nodiscard]] SnapshotData convertSnapshot(
        const libreshockwave::player::debug::DebugSnapshot& snapshot) const;
};

} // namespace libreshockwave::debugger
