#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/vm/TraceListener.hpp"
#include "libreshockwave/player/debug/Breakpoint.hpp"
#include "libreshockwave/player/debug/BreakpointManager.hpp"
#include "libreshockwave/player/debug/DebugSnapshot.hpp"
#include "libreshockwave/player/debug/WatchExpression.hpp"

namespace libreshockwave::player::debug {

enum class DebugState {
    Running,
    Paused,
    Stepping
};

class DebugControllerApi : public lingo::vm::TraceListener {
public:
    ~DebugControllerApi() override = default;

    virtual void setGlobalsSnapshot(std::map<std::string, lingo::Datum> globals) = 0;
    virtual void setLocalsSnapshot(std::map<std::string, lingo::Datum> locals) = 0;
    [[nodiscard]] virtual bool isAwaitingStepContinuation() const = 0;
    virtual void reset() = 0;

    [[nodiscard]] virtual bool isPaused() const = 0;
    [[nodiscard]] virtual DebugState state() const = 0;
    [[nodiscard]] virtual std::optional<DebugSnapshot> currentSnapshot() const = 0;

    virtual void stepInto() = 0;
    virtual void stepOver() = 0;
    virtual void stepOut() = 0;
    virtual void continueExecution() = 0;
    virtual void pause() = 0;

    [[nodiscard]] virtual bool toggleBreakpoint(int scriptId, std::string handlerName, int offset) = 0;
    virtual void clearAllBreakpoints() = 0;
    [[nodiscard]] virtual bool hasBreakpoint(int scriptId, const std::string& handlerName, int offset) const = 0;
    [[nodiscard]] virtual std::optional<Breakpoint> getBreakpoint(int scriptId,
                                                                  const std::string& handlerName,
                                                                  int offset) const = 0;
    [[nodiscard]] virtual BreakpointManager& breakpointManager() = 0;
    [[nodiscard]] virtual const BreakpointManager& breakpointManager() const = 0;
    [[nodiscard]] virtual std::string serializeBreakpoints() const = 0;
    virtual void deserializeBreakpoints(std::string data) = 0;

    [[nodiscard]] virtual WatchExpression addWatchExpression(std::string expression) = 0;
    [[nodiscard]] virtual bool removeWatchExpression(const std::string& id) = 0;
    [[nodiscard]] virtual std::vector<WatchExpression> watchExpressions() const = 0;
    [[nodiscard]] virtual std::vector<WatchExpression> evaluateWatchExpressions() = 0;
    virtual void clearWatchExpressions() = 0;

    [[nodiscard]] virtual std::vector<CallFrame> callStackSnapshot() const = 0;
};

} // namespace libreshockwave::player::debug
