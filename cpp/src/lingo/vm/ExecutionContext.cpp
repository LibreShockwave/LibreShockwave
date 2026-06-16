#include "libreshockwave/lingo/vm/ExecutionContext.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace libreshockwave::lingo::vm {

namespace {

void resetBuiltinControlFlow(builtin::BuiltinContext& context) {
    context.returned = false;
    context.aborted = false;
    context.returnValue = Datum::voidValue();
}

std::optional<Datum> consumeBuiltinReturnValue(builtin::BuiltinContext& context) {
    const bool returned = context.returned || context.aborted;
    Datum returnValue = returned ? context.returnValue : Datum::voidValue();
    resetBuiltinControlFlow(context);
    if (!returned) {
        return std::nullopt;
    }
    return returnValue;
}

} // namespace

ExecutionContext::ExecutionContext(Scope& scope,
                                   chunks::ScriptChunk::Instruction instruction,
                                   builtin::BuiltinRegistry* builtins,
                                   builtin::BuiltinContext* builtinContext,
                                   Callbacks callbacks,
                                   int variableMultiplier,
                                   bool instructionTraceEnabled)
    : scope_(&scope),
      instruction_(instruction),
      argument_(instruction.argument),
      variableMultiplier_(variableMultiplier > 0 ? variableMultiplier : 1),
      instructionTraceEnabled_(instructionTraceEnabled),
      builtins_(builtins),
      builtinContext_(builtinContext),
      callbacks_(std::move(callbacks)) {
    scaledArgument_ = argument_ / variableMultiplier_;
}

void ExecutionContext::setInstruction(chunks::ScriptChunk::Instruction instruction) {
    instruction_ = instruction;
    argument_ = instruction.argument;
    scaledArgument_ = argument_ / variableMultiplier_;
}

Scope& ExecutionContext::scope() {
    return *scope_;
}

const Scope& ExecutionContext::scope() const {
    return *scope_;
}

const chunks::ScriptChunk::Instruction& ExecutionContext::instruction() const {
    return instruction_;
}

int ExecutionContext::argument() const {
    return argument_;
}

int ExecutionContext::scaledArgument() const {
    return scaledArgument_;
}

int ExecutionContext::variableMultiplier() const {
    return variableMultiplier_;
}

int ExecutionContext::instructionOffset() const {
    return instruction_.offset;
}

bool ExecutionContext::instructionTraceEnabled() const {
    return instructionTraceEnabled_;
}

void ExecutionContext::push(Datum value) {
    scope_->push(std::move(value));
}

Datum ExecutionContext::pop() {
    return scope_->pop();
}

Datum ExecutionContext::peek() const {
    return scope_->peek();
}

Datum ExecutionContext::peek(int depth) const {
    return scope_->peek(depth);
}

const Datum& ExecutionContext::peekRef(int depth) const {
    return scope_->peekRef(depth);
}

void ExecutionContext::swap() {
    scope_->swap();
}

std::vector<Datum> ExecutionContext::popArgs(int count) {
    std::vector<Datum> args;
    if (count <= 0) {
        return args;
    }
    args.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        args.push_back(pop());
    }
    std::reverse(args.begin(), args.end());
    return args;
}

Datum ExecutionContext::getLocal(int index) const {
    return scope_->getLocal(index);
}

void ExecutionContext::setLocal(int index, Datum value) {
    if (!callbacks_.variableSetListener) {
        scope_->setLocal(index, std::move(value));
        return;
    }
    const Datum tracedValue = value;
    scope_->setLocal(index, std::move(value));
    callbacks_.variableSetListener("local", "local" + std::to_string(index), tracedValue);
}

Datum ExecutionContext::getParam(int index) const {
    return scope_->getParam(index);
}

void ExecutionContext::setParam(int index, Datum value) {
    if (!callbacks_.variableSetListener) {
        scope_->setParam(index, std::move(value));
        return;
    }
    const Datum tracedValue = value;
    scope_->setParam(index, std::move(value));
    callbacks_.variableSetListener("param", "param" + std::to_string(index), tracedValue);
}

Datum ExecutionContext::getGlobal(std::string_view name) const {
    if (callbacks_.globalGetter) {
        return callbacks_.globalGetter(name);
    }
    return Datum::voidValue();
}

void ExecutionContext::setGlobal(std::string_view name, Datum value) {
    if (!callbacks_.variableSetListener) {
        if (callbacks_.globalSetter) {
            callbacks_.globalSetter(name, std::move(value));
        }
        return;
    }
    const Datum tracedValue = value;
    if (callbacks_.globalSetter) {
        callbacks_.globalSetter(name, std::move(value));
    }
    callbacks_.variableSetListener("global", name, tracedValue);
}

void ExecutionContext::setReturnValue(Datum value) {
    scope_->setReturnValue(std::move(value));
}

void ExecutionContext::setReturned(bool returned) {
    scope_->setReturned(returned);
}

void ExecutionContext::setErrorState(bool errorState) {
    if (callbacks_.errorStateSetter) {
        callbacks_.errorStateSetter(errorState);
    }
}

bool ExecutionContext::hasVariableSetListener() const {
    return static_cast<bool>(callbacks_.variableSetListener);
}

void ExecutionContext::tracePropertySet(std::string_view propName, const Datum& value) const {
    if (callbacks_.variableSetListener) {
        callbacks_.variableSetListener("property", "me." + std::string(propName), value);
    }
}

void ExecutionContext::jumpTo(int targetOffset) {
    int targetIndex = -1;
    if (targetOffset == cachedJumpOffset_) {
        targetIndex = cachedJumpIndex_;
    } else {
        targetIndex = scope_->handler().getInstructionIndex(targetOffset);
        cachedJumpOffset_ = targetOffset;
        cachedJumpIndex_ = targetIndex;
    }
    if (targetIndex >= 0) {
        scope_->setBytecodeIndex(targetIndex);
    }
}

const chunks::ScriptChunk::Handler* ExecutionContext::findLocalHandler(int index) const {
    const auto* script = scope_->script();
    if (script == nullptr || index < 0 || index >= static_cast<int>(script->handlers().size())) {
        return nullptr;
    }
    return &script->handlers()[static_cast<std::size_t>(index)];
}

const std::vector<chunks::ScriptChunk::LiteralEntry>& ExecutionContext::literals() const {
    static const std::vector<chunks::ScriptChunk::LiteralEntry> empty;
    const auto* script = scope_->script();
    return script != nullptr ? script->literals() : empty;
}

const std::string& ExecutionContext::resolveNameRef(int nameId) const {
    if (const auto found = resolvedNames_.find(nameId); found != resolvedNames_.end()) {
        return found->second;
    }
    std::string resolved;
    if (callbacks_.nameResolver) {
        resolved = callbacks_.nameResolver(nameId);
    } else {
        const auto* script = scope_->script();
        resolved = script != nullptr ? script->resolveName(nameId, nullptr) : "#" + std::to_string(nameId);
    }
    auto [inserted, _] = resolvedNames_.emplace(nameId, std::move(resolved));
    return inserted->second;
}

std::string ExecutionContext::resolveName(int nameId) const {
    return resolveNameRef(nameId);
}

std::optional<HandlerRef> ExecutionContext::findHandler(std::string_view name) const {
    if (callbacks_.handlerFinder) {
        return callbacks_.handlerFinder(name);
    }
    return std::nullopt;
}

Datum ExecutionContext::executeHandler(const chunks::ScriptChunk& script,
                                       const chunks::ScriptChunk::Handler& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const {
    return executeHandler(HandlerRef{&script, &handler}, args, receiver);
}

Datum ExecutionContext::executeHandler(const HandlerRef& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const {
    if (callbacks_.handlerRefExecutor) {
        return callbacks_.handlerRefExecutor(handler, args, receiver);
    }
    if (callbacks_.handlerExecutor) {
        if (handler.script == nullptr || handler.handler == nullptr) {
            return Datum::voidValue();
        }
        return callbacks_.handlerExecutor(*handler.script, *handler.handler, args, receiver);
    }
    return Datum::voidValue();
}

bool ExecutionContext::isBuiltin(std::string_view name) const {
    return builtins_ != nullptr && builtins_->contains(name);
}

Datum ExecutionContext::invokeBuiltin(std::string_view name, const std::vector<Datum>& args) {
    if (callbacks_.builtinInvoker) {
        return callbacks_.builtinInvoker(name, args);
    }
    if (builtins_ == nullptr) {
        return Datum::voidValue();
    }
    builtin::BuiltinContext fallback;
    auto& context = builtinContext_ != nullptr ? *builtinContext_ : fallback;
    resetBuiltinControlFlow(context);
    Datum result = builtins_->invoke(name, context, args);
    if (auto returnValue = consumeBuiltinReturnValue(context)) {
        scope_->setReturnValue(std::move(*returnValue));
    }
    return result;
}

std::optional<Datum> ExecutionContext::invokeBuiltinIfPresent(std::string_view name, const std::vector<Datum>& args) {
    if (builtins_ == nullptr) {
        return std::nullopt;
    }
    builtin::BuiltinContext fallback;
    auto& context = builtinContext_ != nullptr ? *builtinContext_ : fallback;
    resetBuiltinControlFlow(context);
    auto result = builtins_->invokeIfPresent(name, context, args);
    if (auto returnValue = consumeBuiltinReturnValue(context)) {
        scope_->setReturnValue(std::move(*returnValue));
    }
    return result;
}

builtin::BuiltinRegistry* ExecutionContext::builtins() {
    return builtins_;
}

const builtin::BuiltinRegistry* ExecutionContext::builtins() const {
    return builtins_;
}

builtin::BuiltinContext* ExecutionContext::builtinContext() {
    return builtinContext_;
}

const builtin::BuiltinContext* ExecutionContext::builtinContext() const {
    return builtinContext_;
}

std::string ExecutionContext::formatCallStack() const {
    if (callbacks_.callStackFormatter) {
        return callbacks_.callStackFormatter();
    }
    return "(no call stack available)";
}

LingoException ExecutionContext::error(const std::string& message) const {
    return LingoException(message);
}

} // namespace libreshockwave::lingo::vm
