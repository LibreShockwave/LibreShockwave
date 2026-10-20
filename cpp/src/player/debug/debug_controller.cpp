#include "libreshockwave/player/debug/debug_controller.hpp"

#include <algorithm>
#include <utility>

namespace libreshockwave::player::debug {

void DebugController::setDelegateListener(std::shared_ptr<lingo::vm::TraceListener> listener) {
    std::lock_guard lock(mutex_);
    delegate_listener_ = std::move(listener);
}

void DebugController::setTracingEnabled(bool enabled) {
    std::lock_guard lock(mutex_);
    tracing_enabled_ = enabled;
}

bool DebugController::tracingEnabled() const {
    std::lock_guard lock(mutex_);
    return tracing_enabled_;
}

void DebugController::addListener(DebugStateListener* listener) {
    if (listener == nullptr) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void DebugController::removeListener(DebugStateListener* listener) {
    std::lock_guard lock(mutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void DebugController::onHandlerEnter(const HandlerInfo& info) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        ++call_depth_;
        current_handler_info_ = info;
        handler_info_stack_.push_back(info);
        call_stack_.push_back(CallFrame{
            info.script_id,
            info.script_display_name,
            info.handler_name,
            info.arguments,
            info.receiver.isVoid() ? std::optional<lingo::Datum>{} : std::optional<lingo::Datum>{info.receiver},
        });
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onHandlerEnter(info);
    }
}

void DebugController::onHandlerExit(const HandlerInfo& info, const lingo::Datum& return_value) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        if (call_depth_ > 0) {
            --call_depth_;
        }
        if (!handler_info_stack_.empty()) {
            handler_info_stack_.pop_back();
        }
        current_handler_info_ = handler_info_stack_.empty()
            ? std::nullopt
            : std::optional<HandlerInfo>{handler_info_stack_.back()};
        if (!call_stack_.empty()) {
            call_stack_.pop_back();
        }
        if (step_mode_ == StepMode::StepOut && call_depth_ <= target_call_depth_) {
            step_mode_ = StepMode::StepInto;
        }
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onHandlerExit(info, return_value);
    }
}

void DebugController::onInstruction(const InstructionInfo& info) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        current_instruction_info_ = info;
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onInstruction(info);
    }

    std::optional<DebugSnapshot> paused_snapshot;
    {
        std::unique_lock lock(mutex_);
        if (checkBreakLocked(info).should_pause) {
            state_ = DebugState::Paused;
            step_mode_ = StepMode::None;
            current_snapshot_ = captureSnapshotLocked(info);
            paused_snapshot = current_snapshot_;
        }
    }
    if (!paused_snapshot.has_value()) {
        return;
    }

    notifyPaused(*paused_snapshot);

    if (!non_blocking_mode_) {
        std::unique_lock lock(mutex_);
        pause_cv_.wait(lock, [this] {
            return state_ != DebugState::Paused;
        });
    }
}

bool DebugController::needsInstructionTrace() const {
    return tracingEnabled();
}

bool DebugController::needsFullInstructionInfo(int offset) const {
    std::lock_guard lock(mutex_);
    if (pause_requested_ || step_mode_ != StepMode::None || !current_handler_info_.has_value()) {
        return true;
    }

    const auto breakpoint = breakpoint_manager_.getBreakpoint(
        current_handler_info_->script_id,
        current_handler_info_->handler_name,
        offset);
    return breakpoint.has_value() && breakpoint->enabled;
}

void DebugController::onVariableSet(std::string_view type, std::string_view name, const lingo::Datum& value) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onVariableSet(type, name, value);
    }
}

void DebugController::onError(std::string_view message, std::string_view error) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onError(message, error);
    }
}

void DebugController::onDebugMessage(std::string_view message) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegate_listener_;
    }
    if (delegate) {
        delegate->onDebugMessage(message);
    }
}

void DebugController::setGlobalsSnapshot(std::map<std::string, lingo::Datum> globals) {
    std::lock_guard lock(mutex_);
    globals_snapshot_ = std::move(globals);
}

void DebugController::setLocalsSnapshot(std::map<std::string, lingo::Datum> locals) {
    std::lock_guard lock(mutex_);
    locals_snapshot_ = std::move(locals);
}

bool DebugController::isAwaitingStepContinuation() const {
    std::lock_guard lock(mutex_);
    return state_ == DebugState::Stepping && step_mode_ != StepMode::None;
}

void DebugController::reset() {
    {
        std::lock_guard lock(mutex_);
        state_ = DebugState::Running;
        step_mode_ = StepMode::None;
        call_depth_ = 0;
        target_call_depth_ = 0;
        pause_requested_ = false;
        handler_info_stack_.clear();
        call_stack_.clear();
        current_handler_info_.reset();
        current_instruction_info_.reset();
        current_snapshot_.reset();
    }
    pause_cv_.notify_all();
}

bool DebugController::isPaused() const {
    std::lock_guard lock(mutex_);
    return state_ == DebugState::Paused;
}

DebugState DebugController::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

StepMode DebugController::stepMode() const {
    std::lock_guard lock(mutex_);
    return step_mode_;
}

int DebugController::callDepth() const {
    std::lock_guard lock(mutex_);
    return call_depth_;
}

std::optional<std::string> DebugController::currentHandlerName() const {
    std::lock_guard lock(mutex_);
    return current_handler_info_.has_value()
        ? std::optional<std::string>{current_handler_info_->handler_name}
        : std::nullopt;
}

std::optional<DebugSnapshot> DebugController::currentSnapshot() const {
    std::lock_guard lock(mutex_);
    return current_snapshot_;
}

void DebugController::stepInto() {
    bool should_notify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        step_mode_ = StepMode::StepInto;
        state_ = DebugState::Stepping;
        should_notify = true;
    }
    if (should_notify) {
        notifyResumed();
        pause_cv_.notify_all();
    }
}

void DebugController::stepOver() {
    bool should_notify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        step_mode_ = StepMode::StepOver;
        target_call_depth_ = call_depth_;
        state_ = DebugState::Stepping;
        should_notify = true;
    }
    if (should_notify) {
        notifyResumed();
        pause_cv_.notify_all();
    }
}

void DebugController::stepOut() {
    bool should_notify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        step_mode_ = StepMode::StepOut;
        target_call_depth_ = call_depth_ - 1;
        state_ = DebugState::Stepping;
        should_notify = true;
    }
    if (should_notify) {
        notifyResumed();
        pause_cv_.notify_all();
    }
}

void DebugController::continueExecution() {
    bool should_notify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        step_mode_ = StepMode::None;
        state_ = DebugState::Running;
        should_notify = true;
    }
    if (should_notify) {
        notifyResumed();
        pause_cv_.notify_all();
    }
}

void DebugController::pause() {
    std::lock_guard lock(mutex_);
    if (state_ == DebugState::Running || state_ == DebugState::Stepping) {
        pause_requested_ = true;
    }
    // In non-blocking mode we must also wake anyone waiting on step
    // completion — there is no one, but keep symmetry with the blocking path.
}

void DebugController::setNonBlockingMode(bool enabled) {
    std::lock_guard lock(mutex_);
    non_blocking_mode_ = enabled;
}

bool DebugController::isNonBlockingMode() const {
    std::lock_guard lock(mutex_);
    return non_blocking_mode_;
}

bool DebugController::toggleBreakpoint(int script_id, std::string handler_name, int offset) {
    bool added = false;
    {
        std::lock_guard lock(mutex_);
        added = breakpoint_manager_.toggleBreakpoint(script_id, std::move(handler_name), offset).has_value();
    }
    notifyBreakpointsChanged();
    return added;
}

Breakpoint DebugController::addBreakpoint(int script_id, std::string handler_name, int offset) {
    Breakpoint breakpoint;
    {
        std::lock_guard lock(mutex_);
        breakpoint = breakpoint_manager_.addBreakpoint(script_id, std::move(handler_name), offset);
    }
    notifyBreakpointsChanged();
    return breakpoint;
}

std::optional<Breakpoint> DebugController::removeBreakpoint(int script_id,
                                                            const std::string& handler_name,
                                                            int offset) {
    std::optional<Breakpoint> removed;
    {
        std::lock_guard lock(mutex_);
        removed = breakpoint_manager_.removeBreakpoint(script_id, handler_name, offset);
    }
    if (removed.has_value()) {
        notifyBreakpointsChanged();
    }
    return removed;
}

std::optional<Breakpoint> DebugController::toggleBreakpointEnabled(int script_id,
                                                                   const std::string& handler_name,
                                                                   int offset) {
    std::optional<Breakpoint> breakpoint;
    {
        std::lock_guard lock(mutex_);
        breakpoint = breakpoint_manager_.toggleEnabled(script_id, handler_name, offset);
    }
    if (breakpoint.has_value()) {
        notifyBreakpointsChanged();
    }
    return breakpoint;
}

void DebugController::setBreakpoint(Breakpoint breakpoint) {
    {
        std::lock_guard lock(mutex_);
        breakpoint_manager_.setBreakpoint(std::move(breakpoint));
    }
    notifyBreakpointsChanged();
}

void DebugController::setBreakpoints(std::map<int, std::set<int>> breakpoints) {
    {
        std::lock_guard lock(mutex_);
        breakpoint_manager_.setFromOffsetMap(breakpoints);
    }
    notifyBreakpointsChanged();
}

std::map<int, std::set<int>> DebugController::breakpoints() const {
    std::lock_guard lock(mutex_);
    return breakpoint_manager_.toOffsetMap();
}

void DebugController::clearAllBreakpoints() {
    {
        std::lock_guard lock(mutex_);
        breakpoint_manager_.clearAll();
    }
    notifyBreakpointsChanged();
}

bool DebugController::hasBreakpoint(int script_id, const std::string& handler_name, int offset) const {
    std::lock_guard lock(mutex_);
    return breakpoint_manager_.hasBreakpoint(script_id, handler_name, offset);
}

std::optional<Breakpoint> DebugController::getBreakpoint(int script_id,
                                                         const std::string& handler_name,
                                                         int offset) const {
    std::lock_guard lock(mutex_);
    return breakpoint_manager_.getBreakpoint(script_id, handler_name, offset);
}

BreakpointManager& DebugController::breakpointManager() {
    return breakpoint_manager_;
}

const BreakpointManager& DebugController::breakpointManager() const {
    return breakpoint_manager_;
}

std::string DebugController::serializeBreakpoints() const {
    std::lock_guard lock(mutex_);
    return breakpoint_manager_.serialize();
}

void DebugController::deserializeBreakpoints(std::string data) {
    {
        std::lock_guard lock(mutex_);
        breakpoint_manager_.deserialize(std::move(data));
    }
    notifyBreakpointsChanged();
}

WatchExpression DebugController::addWatchExpression(std::string expression) {
    WatchExpression watch;
    {
        std::lock_guard lock(mutex_);
        watch = WatchExpression::create(std::move(expression));
        watch_expressions_.push_back(watch);
    }
    notifyWatchExpressionsChanged();
    return watch;
}

bool DebugController::removeWatchExpression(const std::string& id) {
    bool removed = false;
    {
        std::lock_guard lock(mutex_);
        const auto before = watch_expressions_.size();
        watch_expressions_.erase(std::remove_if(watch_expressions_.begin(),
                                               watch_expressions_.end(),
                                               [&id](const WatchExpression& watch) {
                                                   return watch.id == id;
                                               }),
                                watch_expressions_.end());
        removed = watch_expressions_.size() != before;
    }
    if (removed) {
        notifyWatchExpressionsChanged();
    }
    return removed;
}

std::optional<WatchExpression> DebugController::updateWatchExpression(const std::string& id,
                                                                      std::string expression) {
    std::optional<WatchExpression> updated;
    {
        std::lock_guard lock(mutex_);
        for (auto& watch : watch_expressions_) {
            if (watch.id == id) {
                watch = watch.withExpression(std::move(expression));
                updated = watch;
                break;
            }
        }
    }
    if (updated.has_value()) {
        notifyWatchExpressionsChanged();
    }
    return updated;
}

std::vector<WatchExpression> DebugController::watchExpressions() const {
    std::lock_guard lock(mutex_);
    return watch_expressions_;
}

std::vector<WatchExpression> DebugController::evaluateWatchExpressions() {
    std::lock_guard lock(mutex_);
    return evaluateWatchExpressionsLocked();
}

void DebugController::clearWatchExpressions() {
    {
        std::lock_guard lock(mutex_);
        watch_expressions_.clear();
    }
    notifyWatchExpressionsChanged();
}

std::vector<CallFrame> DebugController::callStackSnapshot() const {
    std::lock_guard lock(mutex_);
    return call_stack_;
}

DebugController::BreakResult DebugController::checkBreakLocked(const InstructionInfo& info) {
    if (pause_requested_) {
        pause_requested_ = false;
        return BreakResult{true};
    }

    const bool suppress_breakpoints =
        (step_mode_ == StepMode::StepOver || step_mode_ == StepMode::StepOut) &&
        call_depth_ > target_call_depth_;
    if (!suppress_breakpoints && current_handler_info_.has_value()) {
        const auto breakpoint = breakpoint_manager_.getBreakpoint(
            current_handler_info_->script_id,
            current_handler_info_->handler_name,
            info.offset);
        if (breakpoint.has_value() && breakpoint->enabled) {
            return BreakResult{true};
        }
    }

    bool should_step = false;
    switch (step_mode_) {
        case StepMode::StepInto:
            should_step = true;
            break;
        case StepMode::StepOver:
            should_step = call_depth_ <= target_call_depth_;
            break;
        case StepMode::StepOut:
        case StepMode::None:
            should_step = false;
            break;
    }
    return BreakResult{should_step};
}

DebugSnapshot DebugController::captureSnapshotLocked(const InstructionInfo& info) {
    const auto handler_info = current_handler_info_.value_or(HandlerInfo{});
    const auto locals = !info.locals_snapshot.empty() ? toOrderedMap(info.locals_snapshot) : locals_snapshot_;
    const auto globals = !info.globals_snapshot.empty() ? toOrderedMap(info.globals_snapshot) : globals_snapshot_;
    locals_snapshot_ = locals;
    globals_snapshot_ = globals;
    const auto watches = evaluateWatchExpressionsLocked();

    return DebugSnapshot{
        handler_info.script_id,
        handler_info.script_display_name,
        handler_info.handler_name,
        info.offset,
        info.bytecode_index,
        info.opcode,
        info.argument,
        info.annotation,
        {},
        info.stack_snapshot,
        locals,
        globals,
        handler_info.arguments,
        handler_info.receiver.isVoid() ? std::optional<lingo::Datum>{} : std::optional<lingo::Datum>{handler_info.receiver},
        call_stack_,
        watches,
    };
}

ExpressionEvaluator::EvaluationContext DebugController::buildEvaluationContextLocked() const {
    ExpressionEvaluator::EvaluationContext context;
    context.locals = locals_snapshot_;
    context.globals = globals_snapshot_;
    if (current_handler_info_.has_value()) {
        for (std::size_t index = 0; index < current_handler_info_->arguments.size(); ++index) {
            context.params["arg" + std::to_string(index)] = current_handler_info_->arguments[index];
        }
        if (!current_handler_info_->receiver.isVoid()) {
            context.receiver = current_handler_info_->receiver;
        }
    }
    return context;
}

std::vector<WatchExpression> DebugController::evaluateWatchExpressionsLocked() {
    const auto context = buildEvaluationContextLocked();
    std::vector<WatchExpression> evaluated;
    evaluated.reserve(watch_expressions_.size());
    for (const auto& watch : watch_expressions_) {
        const auto result = expression_evaluator_.evaluate(watch.expression, context);
        evaluated.push_back(result.succeeded()
                                ? watch.withValue(*result.value)
                                : watch.withError(result.error.value_or("Evaluation failed")));
    }
    return evaluated;
}

void DebugController::notifyPaused(const DebugSnapshot& snapshot) {
    std::vector<DebugStateListener*> listeners;
    {
        std::lock_guard lock(mutex_);
        listeners = listenersSnapshotLocked();
    }
    for (auto* listener : listeners) {
        listener->onPaused(snapshot);
    }
}

void DebugController::notifyResumed() {
    std::vector<DebugStateListener*> listeners;
    {
        std::lock_guard lock(mutex_);
        listeners = listenersSnapshotLocked();
    }
    for (auto* listener : listeners) {
        listener->onResumed();
    }
}

void DebugController::notifyBreakpointsChanged() {
    std::vector<DebugStateListener*> listeners;
    {
        std::lock_guard lock(mutex_);
        listeners = listenersSnapshotLocked();
    }
    for (auto* listener : listeners) {
        listener->onBreakpointsChanged();
    }
}

void DebugController::notifyWatchExpressionsChanged() {
    std::vector<DebugStateListener*> listeners;
    {
        std::lock_guard lock(mutex_);
        listeners = listenersSnapshotLocked();
    }
    for (auto* listener : listeners) {
        listener->onWatchExpressionsChanged();
    }
}

std::vector<DebugStateListener*> DebugController::listenersSnapshotLocked() const {
    return listeners_;
}

} // namespace libreshockwave::player::debug
