#include "libreshockwave/lingo/vm/execution_context.hpp"

#include <limits>
#include <string>
#include <utility>

namespace libreshockwave::lingo::vm {

namespace {

void resetBuiltinControlFlow(builtin::BuiltinContext& context) {
    context.returned = false;
    context.aborted = false;
    context.return_value = Datum::voidValue();
}

std::optional<Datum> consumeBuiltinReturnValue(builtin::BuiltinContext& context) {
    const bool returned = context.returned || context.aborted;
    Datum return_value = returned ? std::move(context.return_value) : Datum::voidValue();
    resetBuiltinControlFlow(context);
    if (!returned) {
        return std::nullopt;
    }
    return std::move(return_value);
}

} // namespace

ExecutionContext::ExecutionContext(Scope& scope,
                                   moviesource::ScriptAsset::Instruction instruction,
                                   builtin::BuiltinRegistry* builtins,
                                   builtin::BuiltinContext* builtin_context,
                                   Callbacks callbacks,
                                   int variable_multiplier,
                                   bool instruction_trace_enabled)
    : scope_(&scope),
      instruction_(instruction),
      argument_(instruction.argument),
      variable_multiplier_(variable_multiplier > 0 ? variable_multiplier : 1),
      instruction_trace_enabled_(instruction_trace_enabled),
      builtins_(builtins),
      builtin_context_(builtin_context),
      callbacks_(std::move(callbacks)) {}

std::vector<Datum> ExecutionContext::popArgs(int count) {
    if (count <= 0) {
        return {};
    }
    std::vector<Datum> args(static_cast<std::size_t>(count));
    for (int index = count - 1; index >= 0; --index) {
        args[static_cast<std::size_t>(index)] = pop();
    }
    return args;
}

Datum ExecutionContext::getLocal(int index) const {
    return scope_->getLocal(index);
}

void ExecutionContext::setLocal(int index, Datum value) {
    if (!callbacks_.variable_set_listener) {
        scope_->setLocal(index, std::move(value));
        return;
    }
    const Datum traced_value = value;
    scope_->setLocal(index, std::move(value));
    callbacks_.variable_set_listener("local", "local" + std::to_string(index), traced_value);
}

Datum ExecutionContext::getParam(int index) const {
    return scope_->getParam(index);
}

void ExecutionContext::setParam(int index, Datum value) {
    if (!callbacks_.variable_set_listener) {
        scope_->setParam(index, std::move(value));
        return;
    }
    const Datum traced_value = value;
    scope_->setParam(index, std::move(value));
    callbacks_.variable_set_listener("param", "param" + std::to_string(index), traced_value);
}

Datum ExecutionContext::getGlobal(std::string_view name) const {
    if (callbacks_.global_getter) {
        return callbacks_.global_getter(name);
    }
    return Datum::voidValue();
}

void ExecutionContext::setGlobal(std::string_view name, Datum value) {
    if (!callbacks_.variable_set_listener) {
        if (callbacks_.global_setter) {
            callbacks_.global_setter(name, std::move(value));
        }
        return;
    }
    const Datum traced_value = value;
    if (callbacks_.global_setter) {
        callbacks_.global_setter(name, std::move(value));
    }
    callbacks_.variable_set_listener("global", name, traced_value);
}

void ExecutionContext::setErrorState(bool error_state) {
    if (callbacks_.error_state_setter) {
        callbacks_.error_state_setter(error_state);
    }
}

void ExecutionContext::tracePropertySet(std::string_view prop_name, const Datum& value) const {
    if (callbacks_.variable_set_listener) {
        callbacks_.variable_set_listener("property", "me." + std::string(prop_name), value);
    }
}

void ExecutionContext::jumpTo(int target_offset) {
    int target_index = -1;
    if (target_offset == cached_jump_offset_) {
        target_index = cached_jump_index_;
    } else {
        target_index = scope_->handler().getInstructionIndex(target_offset);
        cached_jump_offset_ = target_offset;
        cached_jump_index_ = target_index;
    }
    if (target_index >= 0) {
        scope_->setBytecodeIndex(target_index);
    }
}

const moviesource::ScriptAsset::Handler* ExecutionContext::findLocalHandler(int index) const {
    const auto* script = scope_->script();
    if (script == nullptr || index < 0 || index >= static_cast<int>(script->handlers.size())) {
        return nullptr;
    }
    return &script->handlers[static_cast<std::size_t>(index)];
}

const std::vector<moviesource::ScriptAsset::LiteralEntry>& ExecutionContext::literals() const {
    static const std::vector<moviesource::ScriptAsset::LiteralEntry> empty;
    const auto* script = scope_->script();
    return script != nullptr ? script->literals : empty;
}

const std::string& ExecutionContext::resolveNameRef(int name_id) const {
    if (name_id == cached_resolved_name_id_ && cached_resolved_name_ != nullptr) {
        return *cached_resolved_name_;
    }
    if (const auto found = resolved_names_.find(name_id); found != resolved_names_.end()) {
        cached_resolved_name_id_ = name_id;
        cached_resolved_name_ = &found->second;
        return found->second;
    }
    std::string resolved;
    if (callbacks_.name_resolver) {
        resolved = callbacks_.name_resolver(name_id);
    } else {
        const auto* script = scope_->script();
        resolved = script != nullptr ? script->resolveName(name_id) : "#" + std::to_string(name_id);
    }
    auto [inserted, _] = resolved_names_.emplace(name_id, std::move(resolved));
    cached_resolved_name_id_ = name_id;
    cached_resolved_name_ = &inserted->second;
    return inserted->second;
}

std::string ExecutionContext::resolveName(int name_id) const {
    return resolveNameRef(name_id);
}

std::optional<HandlerRef> ExecutionContext::findHandler(std::string_view name) const {
    if (callbacks_.handler_finder) {
        return callbacks_.handler_finder(name);
    }
    return std::nullopt;
}

Datum ExecutionContext::executeHandler(const moviesource::ScriptAsset& script,
                                       const moviesource::ScriptAsset::Handler& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const {
    return executeHandler(script, handler, std::span<const Datum>(args), receiver);
}

Datum ExecutionContext::executeHandler(const moviesource::ScriptAsset& script,
                                       const moviesource::ScriptAsset::Handler& handler,
                                       std::span<const Datum> args,
                                       const Datum& receiver) const {
    return executeHandler(HandlerRef{&script, &handler}, args, receiver);
}

Datum ExecutionContext::executeHandler(const HandlerRef& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const {
    return executeHandler(handler, std::span<const Datum>(args), receiver);
}

Datum ExecutionContext::executeHandler(const HandlerRef& handler,
                                       std::span<const Datum> args,
                                       const Datum& receiver) const {
    if (callbacks_.handler_ref_span_executor) {
        return callbacks_.handler_ref_span_executor(handler, args, receiver);
    }
    if (callbacks_.handler_ref_executor) {
        const std::vector<Datum> arg_vector(args.begin(), args.end());
        return callbacks_.handler_ref_executor(handler, arg_vector, receiver);
    }
    if (callbacks_.handler_executor) {
        if (handler.script == nullptr || handler.handler == nullptr) {
            return Datum::voidValue();
        }
        const std::vector<Datum> arg_vector(args.begin(), args.end());
        return callbacks_.handler_executor(*handler.script, *handler.handler, arg_vector, receiver);
    }
    return Datum::voidValue();
}

bool ExecutionContext::isBuiltin(std::string_view name) const {
    return builtins_ != nullptr && builtins_->contains(name);
}

Datum ExecutionContext::invokeBuiltin(std::string_view name, const std::vector<Datum>& args) {
    if (callbacks_.builtin_invoker) {
        return callbacks_.builtin_invoker(name, args);
    }
    if (builtins_ == nullptr) {
        return Datum::voidValue();
    }
    builtin::BuiltinContext fallback;
    auto& context = builtin_context_ != nullptr ? *builtin_context_ : fallback;
    resetBuiltinControlFlow(context);
    Datum result = builtins_->invoke(name, context, args);
    if (auto return_value = consumeBuiltinReturnValue(context)) {
        scope_->setReturnValue(std::move(*return_value));
    }
    return result;
}

std::optional<Datum> ExecutionContext::invokeBuiltinIfPresent(std::string_view name, const std::vector<Datum>& args) {
    if (builtins_ == nullptr) {
        return std::nullopt;
    }
    builtin::BuiltinContext fallback;
    auto& context = builtin_context_ != nullptr ? *builtin_context_ : fallback;
    resetBuiltinControlFlow(context);
    auto result = builtins_->invokeIfPresent(name, context, args);
    if (auto return_value = consumeBuiltinReturnValue(context)) {
        scope_->setReturnValue(std::move(*return_value));
    }
    return result;
}

std::string ExecutionContext::formatCallStack() const {
    if (callbacks_.call_stack_formatter) {
        return callbacks_.call_stack_formatter();
    }
    return "(no call stack available)";
}

LingoException ExecutionContext::error(const std::string& message) const {
    return LingoException(message);
}

} // namespace libreshockwave::lingo::vm
