#include "libreshockwave/player/debug/DebugController.hpp"

#include <algorithm>
#include <utility>

namespace libreshockwave::player::debug {

void DebugController::setDelegateListener(std::shared_ptr<lingo::vm::TraceListener> listener) {
    std::lock_guard lock(mutex_);
    delegateListener_ = std::move(listener);
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
        ++callDepth_;
        currentHandlerInfo_ = info;
        handlerInfoStack_.push_back(info);
        callStack_.push_back(CallFrame{
            info.scriptId,
            info.scriptDisplayName,
            info.handlerName,
            info.arguments,
            info.receiver.isVoid() ? std::optional<lingo::Datum>{} : std::optional<lingo::Datum>{info.receiver},
        });
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onHandlerEnter(info);
    }
}

void DebugController::onHandlerExit(const HandlerInfo& info, const lingo::Datum& returnValue) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        if (callDepth_ > 0) {
            --callDepth_;
        }
        if (!handlerInfoStack_.empty()) {
            handlerInfoStack_.pop_back();
        }
        currentHandlerInfo_ = handlerInfoStack_.empty()
            ? std::nullopt
            : std::optional<HandlerInfo>{handlerInfoStack_.back()};
        if (!callStack_.empty()) {
            callStack_.pop_back();
        }
        if (stepMode_ == StepMode::StepOut && callDepth_ <= targetCallDepth_) {
            stepMode_ = StepMode::StepInto;
        }
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onHandlerExit(info, returnValue);
    }
}

void DebugController::onInstruction(const InstructionInfo& info) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        currentInstructionInfo_ = info;
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onInstruction(info);
    }

    std::optional<DebugSnapshot> pausedSnapshot;
    {
        std::unique_lock lock(mutex_);
        if (checkBreakLocked(info).shouldPause) {
            state_ = DebugState::Paused;
            stepMode_ = StepMode::None;
            currentSnapshot_ = captureSnapshotLocked(info);
            pausedSnapshot = currentSnapshot_;
        }
    }
    if (!pausedSnapshot.has_value()) {
        return;
    }

    notifyPaused(*pausedSnapshot);

    std::unique_lock lock(mutex_);
    pauseCv_.wait(lock, [this] {
        return state_ != DebugState::Paused;
    });
}

bool DebugController::needsInstructionTrace() const {
    return true;
}

void DebugController::onVariableSet(std::string_view type, std::string_view name, const lingo::Datum& value) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onVariableSet(type, name, value);
    }
}

void DebugController::onError(std::string_view message, std::string_view error) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onError(message, error);
    }
}

void DebugController::onDebugMessage(std::string_view message) {
    std::shared_ptr<lingo::vm::TraceListener> delegate;
    {
        std::lock_guard lock(mutex_);
        delegate = delegateListener_;
    }
    if (delegate) {
        delegate->onDebugMessage(message);
    }
}

void DebugController::setGlobalsSnapshot(std::map<std::string, lingo::Datum> globals) {
    std::lock_guard lock(mutex_);
    globalsSnapshot_ = std::move(globals);
}

void DebugController::setLocalsSnapshot(std::map<std::string, lingo::Datum> locals) {
    std::lock_guard lock(mutex_);
    localsSnapshot_ = std::move(locals);
}

bool DebugController::isAwaitingStepContinuation() const {
    std::lock_guard lock(mutex_);
    return state_ == DebugState::Stepping && stepMode_ != StepMode::None;
}

void DebugController::reset() {
    {
        std::lock_guard lock(mutex_);
        state_ = DebugState::Running;
        stepMode_ = StepMode::None;
        callDepth_ = 0;
        targetCallDepth_ = 0;
        pauseRequested_ = false;
        handlerInfoStack_.clear();
        callStack_.clear();
        currentHandlerInfo_.reset();
        currentInstructionInfo_.reset();
        currentSnapshot_.reset();
    }
    pauseCv_.notify_all();
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
    return stepMode_;
}

int DebugController::callDepth() const {
    std::lock_guard lock(mutex_);
    return callDepth_;
}

std::optional<std::string> DebugController::currentHandlerName() const {
    std::lock_guard lock(mutex_);
    return currentHandlerInfo_.has_value()
        ? std::optional<std::string>{currentHandlerInfo_->handlerName}
        : std::nullopt;
}

std::optional<DebugSnapshot> DebugController::currentSnapshot() const {
    std::lock_guard lock(mutex_);
    return currentSnapshot_;
}

void DebugController::stepInto() {
    bool shouldNotify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        stepMode_ = StepMode::StepInto;
        state_ = DebugState::Stepping;
        shouldNotify = true;
    }
    if (shouldNotify) {
        notifyResumed();
        pauseCv_.notify_all();
    }
}

void DebugController::stepOver() {
    bool shouldNotify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        stepMode_ = StepMode::StepOver;
        targetCallDepth_ = callDepth_;
        state_ = DebugState::Stepping;
        shouldNotify = true;
    }
    if (shouldNotify) {
        notifyResumed();
        pauseCv_.notify_all();
    }
}

void DebugController::stepOut() {
    bool shouldNotify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        stepMode_ = StepMode::StepOut;
        targetCallDepth_ = callDepth_ - 1;
        state_ = DebugState::Stepping;
        shouldNotify = true;
    }
    if (shouldNotify) {
        notifyResumed();
        pauseCv_.notify_all();
    }
}

void DebugController::continueExecution() {
    bool shouldNotify = false;
    {
        std::lock_guard lock(mutex_);
        if (state_ != DebugState::Paused) {
            return;
        }
        stepMode_ = StepMode::None;
        state_ = DebugState::Running;
        shouldNotify = true;
    }
    if (shouldNotify) {
        notifyResumed();
        pauseCv_.notify_all();
    }
}

void DebugController::pause() {
    std::lock_guard lock(mutex_);
    if (state_ == DebugState::Running || state_ == DebugState::Stepping) {
        pauseRequested_ = true;
    }
}

bool DebugController::toggleBreakpoint(int scriptId, std::string handlerName, int offset) {
    bool added = false;
    {
        std::lock_guard lock(mutex_);
        added = breakpointManager_.toggleBreakpoint(scriptId, std::move(handlerName), offset).has_value();
    }
    notifyBreakpointsChanged();
    return added;
}

Breakpoint DebugController::addBreakpoint(int scriptId, std::string handlerName, int offset) {
    Breakpoint breakpoint;
    {
        std::lock_guard lock(mutex_);
        breakpoint = breakpointManager_.addBreakpoint(scriptId, std::move(handlerName), offset);
    }
    notifyBreakpointsChanged();
    return breakpoint;
}

std::optional<Breakpoint> DebugController::removeBreakpoint(int scriptId,
                                                            const std::string& handlerName,
                                                            int offset) {
    std::optional<Breakpoint> removed;
    {
        std::lock_guard lock(mutex_);
        removed = breakpointManager_.removeBreakpoint(scriptId, handlerName, offset);
    }
    if (removed.has_value()) {
        notifyBreakpointsChanged();
    }
    return removed;
}

std::optional<Breakpoint> DebugController::toggleBreakpointEnabled(int scriptId,
                                                                   const std::string& handlerName,
                                                                   int offset) {
    std::optional<Breakpoint> breakpoint;
    {
        std::lock_guard lock(mutex_);
        breakpoint = breakpointManager_.toggleEnabled(scriptId, handlerName, offset);
    }
    if (breakpoint.has_value()) {
        notifyBreakpointsChanged();
    }
    return breakpoint;
}

void DebugController::setBreakpoint(Breakpoint breakpoint) {
    {
        std::lock_guard lock(mutex_);
        breakpointManager_.setBreakpoint(std::move(breakpoint));
    }
    notifyBreakpointsChanged();
}

void DebugController::setBreakpoints(std::map<int, std::set<int>> breakpoints) {
    {
        std::lock_guard lock(mutex_);
        breakpointManager_.setFromOffsetMap(breakpoints);
    }
    notifyBreakpointsChanged();
}

std::map<int, std::set<int>> DebugController::breakpoints() const {
    std::lock_guard lock(mutex_);
    return breakpointManager_.toOffsetMap();
}

void DebugController::clearAllBreakpoints() {
    {
        std::lock_guard lock(mutex_);
        breakpointManager_.clearAll();
    }
    notifyBreakpointsChanged();
}

bool DebugController::hasBreakpoint(int scriptId, const std::string& handlerName, int offset) const {
    std::lock_guard lock(mutex_);
    return breakpointManager_.hasBreakpoint(scriptId, handlerName, offset);
}

std::optional<Breakpoint> DebugController::getBreakpoint(int scriptId,
                                                         const std::string& handlerName,
                                                         int offset) const {
    std::lock_guard lock(mutex_);
    return breakpointManager_.getBreakpoint(scriptId, handlerName, offset);
}

BreakpointManager& DebugController::breakpointManager() {
    return breakpointManager_;
}

const BreakpointManager& DebugController::breakpointManager() const {
    return breakpointManager_;
}

std::string DebugController::serializeBreakpoints() const {
    std::lock_guard lock(mutex_);
    return breakpointManager_.serialize();
}

void DebugController::deserializeBreakpoints(std::string data) {
    {
        std::lock_guard lock(mutex_);
        breakpointManager_.deserialize(std::move(data));
    }
    notifyBreakpointsChanged();
}

WatchExpression DebugController::addWatchExpression(std::string expression) {
    WatchExpression watch;
    {
        std::lock_guard lock(mutex_);
        watch = WatchExpression::create(std::move(expression));
        watchExpressions_.push_back(watch);
    }
    notifyWatchExpressionsChanged();
    return watch;
}

bool DebugController::removeWatchExpression(const std::string& id) {
    bool removed = false;
    {
        std::lock_guard lock(mutex_);
        const auto before = watchExpressions_.size();
        watchExpressions_.erase(std::remove_if(watchExpressions_.begin(),
                                               watchExpressions_.end(),
                                               [&id](const WatchExpression& watch) {
                                                   return watch.id == id;
                                               }),
                                watchExpressions_.end());
        removed = watchExpressions_.size() != before;
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
        for (auto& watch : watchExpressions_) {
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
    return watchExpressions_;
}

std::vector<WatchExpression> DebugController::evaluateWatchExpressions() {
    std::lock_guard lock(mutex_);
    return evaluateWatchExpressionsLocked();
}

void DebugController::clearWatchExpressions() {
    {
        std::lock_guard lock(mutex_);
        watchExpressions_.clear();
    }
    notifyWatchExpressionsChanged();
}

std::vector<CallFrame> DebugController::callStackSnapshot() const {
    std::lock_guard lock(mutex_);
    return callStack_;
}

DebugController::BreakResult DebugController::checkBreakLocked(const InstructionInfo& info) {
    if (pauseRequested_) {
        pauseRequested_ = false;
        return BreakResult{true};
    }

    const bool suppressBreakpoints =
        (stepMode_ == StepMode::StepOver || stepMode_ == StepMode::StepOut) &&
        callDepth_ > targetCallDepth_;
    if (!suppressBreakpoints && currentHandlerInfo_.has_value()) {
        const auto breakpoint = breakpointManager_.getBreakpoint(
            currentHandlerInfo_->scriptId,
            currentHandlerInfo_->handlerName,
            info.offset);
        if (breakpoint.has_value() && breakpoint->enabled) {
            return BreakResult{true};
        }
    }

    bool shouldStep = false;
    switch (stepMode_) {
        case StepMode::StepInto:
            shouldStep = true;
            break;
        case StepMode::StepOver:
            shouldStep = callDepth_ <= targetCallDepth_;
            break;
        case StepMode::StepOut:
        case StepMode::None:
            shouldStep = false;
            break;
    }
    return BreakResult{shouldStep};
}

DebugSnapshot DebugController::captureSnapshotLocked(const InstructionInfo& info) {
    const auto handlerInfo = currentHandlerInfo_.value_or(HandlerInfo{});
    const auto locals = !info.localsSnapshot.empty() ? toOrderedMap(info.localsSnapshot) : localsSnapshot_;
    const auto globals = !info.globalsSnapshot.empty() ? toOrderedMap(info.globalsSnapshot) : globalsSnapshot_;
    localsSnapshot_ = locals;
    globalsSnapshot_ = globals;
    const auto watches = evaluateWatchExpressionsLocked();

    return DebugSnapshot{
        handlerInfo.scriptId,
        handlerInfo.scriptDisplayName,
        handlerInfo.handlerName,
        info.offset,
        info.bytecodeIndex,
        info.opcode,
        info.argument,
        info.annotation,
        {},
        info.stackSnapshot,
        locals,
        globals,
        handlerInfo.arguments,
        handlerInfo.receiver.isVoid() ? std::optional<lingo::Datum>{} : std::optional<lingo::Datum>{handlerInfo.receiver},
        callStack_,
        watches,
    };
}

ExpressionEvaluator::EvaluationContext DebugController::buildEvaluationContextLocked() const {
    ExpressionEvaluator::EvaluationContext context;
    context.locals = localsSnapshot_;
    context.globals = globalsSnapshot_;
    if (currentHandlerInfo_.has_value()) {
        for (std::size_t index = 0; index < currentHandlerInfo_->arguments.size(); ++index) {
            context.params["arg" + std::to_string(index)] = currentHandlerInfo_->arguments[index];
        }
        if (!currentHandlerInfo_->receiver.isVoid()) {
            context.receiver = currentHandlerInfo_->receiver;
        }
    }
    return context;
}

std::vector<WatchExpression> DebugController::evaluateWatchExpressionsLocked() {
    const auto context = buildEvaluationContextLocked();
    std::vector<WatchExpression> evaluated;
    evaluated.reserve(watchExpressions_.size());
    for (const auto& watch : watchExpressions_) {
        const auto result = expressionEvaluator_.evaluate(watch.expression, context);
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

std::map<std::string, lingo::Datum> DebugController::toOrderedMap(
    const std::unordered_map<std::string, lingo::Datum>& values) {
    std::map<std::string, lingo::Datum> ordered;
    for (const auto& [key, value] : values) {
        ordered[key] = value;
    }
    return ordered;
}

} // namespace libreshockwave::player::debug
