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

#include "libreshockwave/lingo/vm/trace_listener.hpp"
#include "libreshockwave/player/debug/debug_controller_api.hpp"
#include "libreshockwave/player/debug/debug_state_listener.hpp"
#include "libreshockwave/player/debug/expression_evaluator.hpp"

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
    void setTracingEnabled(bool enabled) override;
    [[nodiscard]] bool tracingEnabled() const override;

    void addListener(DebugStateListener* listener);
    void removeListener(DebugStateListener* listener);

    void onHandlerEnter(const HandlerInfo& info) override;
    void onHandlerExit(const HandlerInfo& info, const lingo::Datum& return_value) override;
    void onInstruction(const InstructionInfo& info) override;
    [[nodiscard]] bool needsInstructionTrace() const override;
    [[nodiscard]] bool needsFullInstructionInfo(int offset) const override;
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

    /// Enable non-blocking mode for single-threaded environments (WASM).
    /// When enabled, onInstruction sets the paused state but returns
    /// immediately instead of blocking on a condition variable.  The VM
    /// checks isExecutionSuspended() after every traced instruction and
    /// unwinds the handler cooperatively.
    void setNonBlockingMode(bool enabled);
    [[nodiscard]] bool isNonBlockingMode() const;

    void stepInto() override;
    void stepOver() override;
    void stepOut() override;
    void continueExecution() override;
    void pause() override;

    [[nodiscard]] bool toggleBreakpoint(int script_id, std::string handler_name, int offset) override;
    [[nodiscard]] Breakpoint addBreakpoint(int script_id, std::string handler_name, int offset);
    [[nodiscard]] std::optional<Breakpoint> removeBreakpoint(int script_id,
                                                             const std::string& handler_name,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleBreakpointEnabled(int script_id,
                                                                    const std::string& handler_name,
                                                                    int offset);
    virtual void setBreakpoint(Breakpoint breakpoint);
    virtual void setBreakpoints(std::map<int, std::set<int>> breakpoints);
    [[nodiscard]] std::map<int, std::set<int>> breakpoints() const;
    void clearAllBreakpoints() override;
    [[nodiscard]] bool hasBreakpoint(int script_id, const std::string& handler_name, int offset) const override;
    [[nodiscard]] std::optional<Breakpoint> getBreakpoint(int script_id,
                                                          const std::string& handler_name,
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
        bool should_pause{false};
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
    std::condition_variable pause_cv_;
    std::shared_ptr<lingo::vm::TraceListener> delegate_listener_;
    BreakpointManager breakpoint_manager_;
    ExpressionEvaluator expression_evaluator_;
    std::vector<WatchExpression> watch_expressions_;
    std::vector<DebugStateListener*> listeners_;
    std::vector<HandlerInfo> handler_info_stack_;
    std::vector<CallFrame> call_stack_;
    std::map<std::string, lingo::Datum> globals_snapshot_;
    std::map<std::string, lingo::Datum> locals_snapshot_;
    std::optional<HandlerInfo> current_handler_info_;
    std::optional<InstructionInfo> current_instruction_info_;
    std::optional<DebugSnapshot> current_snapshot_;
    DebugState state_{DebugState::Running};
    StepMode step_mode_{StepMode::None};
    int call_depth_{0};
    int target_call_depth_{0};
    bool pause_requested_{false};
    bool non_blocking_mode_{false};
    bool tracing_enabled_{true};
};

} // namespace libreshockwave::player::debug
