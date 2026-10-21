#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/builtin/builtin_registry.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"
#include "libreshockwave/lingo/vm/scope.hpp"

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}

}

namespace libreshockwave::lingo::vm {

struct HandlerRef {
    HandlerRef() = default;
    HandlerRef(const moviesource::ScriptAsset* script_value,
               const moviesource::ScriptAsset::Handler* handler_value,
               std::shared_ptr<const moviesource::ScriptAsset> script_owner_value = nullptr,
               std::shared_ptr<const moviesource::MovieSource> file_owner_value = nullptr,
               moviesource::ScriptType script_type_value = moviesource::ScriptType::Unknown)
        : script(script_value),
          handler(handler_value),
          script_owner(std::move(script_owner_value)),
          file_owner(std::move(file_owner_value)),
          script_type(script_type_value) {}
    HandlerRef(const moviesource::ScriptAsset* script_value,
               const moviesource::ScriptAsset::Handler& handler_value,
               std::shared_ptr<const moviesource::ScriptAsset> script_owner_value = nullptr,
               std::shared_ptr<const moviesource::MovieSource> file_owner_value = nullptr,
               moviesource::ScriptType script_type_value = moviesource::ScriptType::Unknown)
        : HandlerRef(script_value,
                     &handler_value,
                     std::move(script_owner_value),
                     std::move(file_owner_value),
                     script_type_value) {}

    const moviesource::ScriptAsset* script{nullptr};
    const moviesource::ScriptAsset::Handler* handler{nullptr};
    std::shared_ptr<const moviesource::ScriptAsset> script_owner;
    std::shared_ptr<const moviesource::MovieSource> file_owner;
    moviesource::ScriptType script_type{moviesource::ScriptType::Unknown};
};

class ExecutionContext {
public:
    using HandlerExecutor = std::function<Datum(const moviesource::ScriptAsset& script,
                                                const moviesource::ScriptAsset::Handler& handler,
                                                const std::vector<Datum>& args,
                                                const Datum& receiver)>;
    using HandlerRefExecutor = std::function<Datum(const HandlerRef& handler,
                                                   const std::vector<Datum>& args,
                                                   const Datum& receiver)>;
    using HandlerRefSpanExecutor = std::function<Datum(const HandlerRef& handler,
                                                       std::span<const Datum> args,
                                                       const Datum& receiver)>;
    using NameResolver = std::function<std::string(int name_id)>;
    using HandlerFinder = std::function<std::optional<HandlerRef>(std::string_view name)>;
    using GlobalGetter = std::function<Datum(std::string_view name)>;
    using GlobalSetter = std::function<void(std::string_view name, Datum value)>;
    using BuiltinInvoker = std::function<Datum(std::string_view name, const std::vector<Datum>& args)>;
    using ErrorStateSetter = std::function<void(bool error_state)>;
    using CallStackFormatter = std::function<std::string()>;
    using VariableSetListener = std::function<void(std::string_view type,
                                                   std::string_view name,
                                                   const Datum& value)>;

    struct Callbacks {
        HandlerExecutor handler_executor;
        HandlerRefExecutor handler_ref_executor;
        HandlerRefSpanExecutor handler_ref_span_executor;
        NameResolver name_resolver;
        HandlerFinder handler_finder;
        GlobalGetter global_getter;
        GlobalSetter global_setter;
        BuiltinInvoker builtin_invoker;
        ErrorStateSetter error_state_setter;
        CallStackFormatter call_stack_formatter;
        VariableSetListener variable_set_listener;
    };

    ExecutionContext(Scope& scope,
                     moviesource::ScriptAsset::Instruction instruction,
                     builtin::BuiltinRegistry* builtins = nullptr,
                     builtin::BuiltinContext* builtin_context = nullptr,
                     Callbacks callbacks = {},
                     int variable_multiplier = 1,
                     bool instruction_trace_enabled = false);

    void setInstruction(moviesource::ScriptAsset::Instruction instruction) {
        instruction_ = instruction;
        argument_ = instruction.argument;
    }

    [[nodiscard]] Scope& scope() { return *scope_; }
    [[nodiscard]] const Scope& scope() const { return *scope_; }
    [[nodiscard]] const moviesource::ScriptAsset::Instruction& instruction() const { return instruction_; }
    [[nodiscard]] int argument() const { return argument_; }
    [[nodiscard]] int scaledArgument() const {
        if (variable_multiplier_ == 1) {
            return argument_;
        }
        if (variable_multiplier_ == 8) {
            return argument_ / 8;
        }
        return argument_ / variable_multiplier_;
    }
    [[nodiscard]] int variableMultiplier() const { return variable_multiplier_; }
    [[nodiscard]] int instructionOffset() const { return instruction_.offset; }
    [[nodiscard]] bool instructionTraceEnabled() const { return instruction_trace_enabled_; }

    void push(Datum value) { scope_->push(std::move(value)); }
    [[nodiscard]] Datum pop() { return scope_->pop(); }
    [[nodiscard]] Datum peek() const { return scope_->peek(); }
    [[nodiscard]] Datum peek(int depth) const { return scope_->peek(depth); }
    [[nodiscard]] const Datum& peekRef(int depth = 0) const { return scope_->peekRef(depth); }
    void swap() { scope_->swap(); }
    [[nodiscard]] std::vector<Datum> popArgs(int count);

    [[nodiscard]] Datum getLocal(int index) const;
    void setLocal(int index, Datum value);
    [[nodiscard]] Datum getParam(int index) const;
    void setParam(int index, Datum value);

    [[nodiscard]] Datum getGlobal(std::string_view name) const;
    void setGlobal(std::string_view name, Datum value);

    void setReturnValue(Datum value) { scope_->setReturnValue(std::move(value)); }
    void setReturned(bool returned) { scope_->setReturned(returned); }
    void setErrorState(bool error_state);
    [[nodiscard]] bool hasVariableSetListener() const { return static_cast<bool>(callbacks_.variable_set_listener); }
    void tracePropertySet(std::string_view prop_name, const Datum& value) const;

    void jumpTo(int target_offset);
    [[nodiscard]] const moviesource::ScriptAsset::Handler* findLocalHandler(int index) const;
    [[nodiscard]] const std::vector<moviesource::ScriptAsset::LiteralEntry>& literals() const;
    [[nodiscard]] const std::string& resolveNameRef(int name_id) const;
    [[nodiscard]] std::string resolveName(int name_id) const;
    [[nodiscard]] std::optional<HandlerRef> findHandler(std::string_view name) const;
    [[nodiscard]] Datum executeHandler(const moviesource::ScriptAsset& script,
                                       const moviesource::ScriptAsset::Handler& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const;
    [[nodiscard]] Datum executeHandler(const moviesource::ScriptAsset& script,
                                       const moviesource::ScriptAsset::Handler& handler,
                                       std::span<const Datum> args,
                                       const Datum& receiver) const;
    [[nodiscard]] Datum executeHandler(const HandlerRef& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const;
    [[nodiscard]] Datum executeHandler(const HandlerRef& handler,
                                       std::span<const Datum> args,
                                       const Datum& receiver) const;

    [[nodiscard]] bool isBuiltin(std::string_view name) const;
    [[nodiscard]] Datum invokeBuiltin(std::string_view name, const std::vector<Datum>& args);
    [[nodiscard]] std::optional<Datum> invokeBuiltinIfPresent(std::string_view name, const std::vector<Datum>& args);
    [[nodiscard]] builtin::BuiltinRegistry* builtins() { return builtins_; }
    [[nodiscard]] const builtin::BuiltinRegistry* builtins() const { return builtins_; }
    [[nodiscard]] builtin::BuiltinContext* builtinContext() { return builtin_context_; }
    [[nodiscard]] const builtin::BuiltinContext* builtinContext() const { return builtin_context_; }

    [[nodiscard]] std::string formatCallStack() const;
    [[nodiscard]] LingoException error(const std::string& message) const;

private:
    Scope* scope_;
    moviesource::ScriptAsset::Instruction instruction_;
    int argument_;
    int variable_multiplier_;
    bool instruction_trace_enabled_;
    int cached_jump_offset_ = -2147483648;
    int cached_jump_index_ = -1;
    builtin::BuiltinRegistry* builtins_;
    builtin::BuiltinContext* builtin_context_;
    Callbacks callbacks_;
    mutable std::unordered_map<int, std::string> resolved_names_;
    mutable int cached_resolved_name_id_ = -2147483648;
    mutable const std::string* cached_resolved_name_ = nullptr;
};

} // namespace libreshockwave::lingo::vm
