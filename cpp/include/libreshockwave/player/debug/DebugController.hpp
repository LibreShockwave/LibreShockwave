#pragma once

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/vm/TraceListener.hpp"
#include "libreshockwave/player/debug/DebugControllerApi.hpp"
#include "libreshockwave/player/debug/DebugStateListener.hpp"
#include "libreshockwave/player/debug/ExpressionEvaluator.hpp"

namespace libreshockwave::player::debug {

enum class StepMode {
    None,
    StepInto,
    StepOver,
    StepOut
};

class DebugController final : public DebugControllerApi {
public:
    DebugController() = default;

    void setDelegateListener(std::shared_ptr<lingo::vm::TraceListener> listener);

    void addListener(DebugStateListener* listener);
    void removeListener(DebugStateListener* listener);

    void onHandlerEnter(const HandlerInfo& info) override;
    void onHandlerExit(const HandlerInfo& info, const lingo::Datum& returnValue) override;
    void onInstruction(const InstructionInfo& info) override;
    [[nodiscard]] bool needsInstructionTrace() const override;
    void onVariableSet(std::string_view type, std::string_view name, const lingo::Datum& value) override;
    void onError(std::string_view message, std::string_view error) override;
    void onDebugMessage(std::string_view message) override;

    void setGlobalsSnapshot(std::map<std::string, lingo::Datum> globals) override;
    void setLocalsSnapshot(std::map<std::string, lingo::Datum> locals) override;
    [[nodiscard]] bool isAwaitingStepContinuation() const override;
    void reset() override;

    [[nodiscard]] bool isPaused() const override;
    [[nodiscard]] DebugState state() const override;
    [[nodiscard]] StepMode stepMode() const;
    [[nodiscard]] int callDepth() const;
    [[nodiscard]] std::optional<std::string> currentHandlerName() const;
    [[nodiscard]] std::optional<DebugSnapshot> currentSnapshot() const override;

    void stepInto() override;
    void stepOver() override;
    void stepOut() override;
    void continueExecution() override;
    void pause() override;

    [[nodiscard]] bool toggleBreakpoint(int scriptId, std::string handlerName, int offset) override;
    [[nodiscard]] Breakpoint addBreakpoint(int scriptId, std::string handlerName, int offset);
    [[nodiscard]] std::optional<Breakpoint> removeBreakpoint(int scriptId,
                                                             const std::string& handlerName,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleBreakpointEnabled(int scriptId,
                                                                    const std::string& handlerName,
                                                                    int offset);
    virtual void setBreakpoint(Breakpoint breakpoint);
    virtual void setBreakpoints(std::map<int, std::set<int>> breakpoints);
    [[nodiscard]] std::map<int, std::set<int>> breakpoints() const;
    void clearAllBreakpoints() override;
    [[nodiscard]] bool hasBreakpoint(int scriptId, const std::string& handlerName, int offset) const override;
    [[nodiscard]] std::optional<Breakpoint> getBreakpoint(int scriptId,
                                                          const std::string& handlerName,
                                                          int offset) const override;
    [[nodiscard]] BreakpointManager& breakpointManager() override;
    [[nodiscard]] const BreakpointManager& breakpointManager() const override;
    [[nodiscard]] std::string serializeBreakpoints() const override;
    void deserializeBreakpoints(std::string data) override;

    [[nodiscard]] WatchExpression addWatchExpression(std::string expression) override;
    [[nodiscard]] bool removeWatchExpression(const std::string& id) override;
    [[nodiscard]] std::optional<WatchExpression> updateWatchExpression(const std::string& id,
                                                                       std::string expression);
    [[nodiscard]] std::vector<WatchExpression> watchExpressions() const override;
    [[nodiscard]] std::vector<WatchExpression> evaluateWatchExpressions() override;
    void clearWatchExpressions() override;

    [[nodiscard]] std::vector<CallFrame> callStackSnapshot() const override;

private:
    struct BreakResult {
        bool shouldPause{false};
    };

    [[nodiscard]] BreakResult checkBreakLocked(const InstructionInfo& info);
    [[nodiscard]] DebugSnapshot captureSnapshotLocked(const InstructionInfo& info);
    [[nodiscard]] ExpressionEvaluator::EvaluationContext buildEvaluationContextLocked() const;
    [[nodiscard]] std::vector<WatchExpression> evaluateWatchExpressionsLocked();
    void notifyPaused(const DebugSnapshot& snapshot);
    void notifyResumed();
    void notifyBreakpointsChanged();
    void notifyWatchExpressionsChanged();
    [[nodiscard]] std::vector<DebugStateListener*> listenersSnapshotLocked() const;
    template <typename Map>
    [[nodiscard]] static std::map<std::string, lingo::Datum> toOrderedMap(const Map& values) {
        std::map<std::string, lingo::Datum> ordered;
        for (const auto& [key, value] : values) {
            ordered[key] = value;
        }
        return ordered;
    }

    mutable std::mutex mutex_;
    std::condition_variable pauseCv_;
    std::shared_ptr<lingo::vm::TraceListener> delegateListener_;
    BreakpointManager breakpointManager_;
    ExpressionEvaluator expressionEvaluator_;
    std::vector<WatchExpression> watchExpressions_;
    std::vector<DebugStateListener*> listeners_;
    std::vector<HandlerInfo> handlerInfoStack_;
    std::vector<CallFrame> callStack_;
    std::map<std::string, lingo::Datum> globalsSnapshot_;
    std::map<std::string, lingo::Datum> localsSnapshot_;
    std::optional<HandlerInfo> currentHandlerInfo_;
    std::optional<InstructionInfo> currentInstructionInfo_;
    std::optional<DebugSnapshot> currentSnapshot_;
    DebugState state_{DebugState::Running};
    StepMode stepMode_{StepMode::None};
    int callDepth_{0};
    int targetCallDepth_{0};
    bool pauseRequested_{false};
};

} // namespace libreshockwave::player::debug
