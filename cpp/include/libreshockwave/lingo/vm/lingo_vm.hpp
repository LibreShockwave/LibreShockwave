#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/builtin/builtin_registry.hpp"
#include "libreshockwave/lingo/vm/alert_hook_handler.hpp"
#include "libreshockwave/lingo/vm/execution_context.hpp"
#include "libreshockwave/lingo/vm/opcode_registry.hpp"
#include "libreshockwave/lingo/vm/scope.hpp"
#include "libreshockwave/lingo/vm/trace_listener.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::vm {

class LingoVM {
public:
    using TraceOutputHandler = std::function<void(std::string_view line)>;
    using GlobalHandlerFinder = std::function<std::optional<HandlerRef>(std::string_view handler_name)>;

    struct CallStackFrame {
        std::string handler_name;
        std::string script_name;
        int bytecode_index{0};
        std::vector<std::string> arguments;

        friend bool operator==(const CallStackFrame&, const CallStackFrame&) = default;
    };

    explicit LingoVM(DirectorFile* file = nullptr);

    [[nodiscard]] DirectorFile* file() const;
    [[nodiscard]] builtin::BuiltinRegistry& builtinRegistry();
    [[nodiscard]] const builtin::BuiltinRegistry& builtinRegistry() const;
    [[nodiscard]] builtin::BuiltinContext& builtinContext();
    [[nodiscard]] const builtin::BuiltinContext& builtinContext() const;
    [[nodiscard]] OpcodeRegistry& opcodeRegistry();
    [[nodiscard]] const OpcodeRegistry& opcodeRegistry() const;

    [[nodiscard]] Datum getGlobal(std::string_view name) const;
    void setGlobal(std::string name, Datum value);
    [[nodiscard]] const RuntimeGlobals& globals() const;
    void clearGlobals();

    [[nodiscard]] Datum getPref(std::string_view name) const;
    [[nodiscard]] Datum setPref(std::string name, const Datum& value);
    [[nodiscard]] const std::map<std::string, Datum>& prefs() const;

    void setRandomSeed(int seed);
    [[nodiscard]] int randomSeed() const;
    [[nodiscard]] int randomInt(int max);

    void setStepLimit(int limit);
    [[nodiscard]] int stepLimit() const;
    void setTickDeadlineMs(std::int64_t milliseconds);
    [[nodiscard]] std::int64_t tickDeadlineMs() const;
    void setTickDeadline(std::int64_t deadline_millis);
    [[nodiscard]] std::int64_t tickDeadline() const;
    void setHandlerTimeoutMs(std::int64_t milliseconds);
    [[nodiscard]] std::int64_t handlerTimeoutMs() const;
    void setSlowHandlerWarningThresholdMs(std::int64_t milliseconds);
    [[nodiscard]] std::int64_t slowHandlerWarningThresholdMs() const;
    void armTickDeadline();
    void setTimeProvider(std::function<std::int64_t()> provider);
    static void setGcCallback(std::function<void()> callback);

    void setPassCallback(std::function<void()> callback);
    void clearPassCallback();
    [[nodiscard]] bool eventStopped() const;
    void resetEventStopped();

    void setErrorState(bool error_state);
    [[nodiscard]] bool isInErrorState() const;
    void resetErrorState();
    void setErrorHandlerSkipCallback(AlertHookHandler::SkipCallback callback);

    void setTraceListener(std::shared_ptr<TraceListener> listener);
    [[nodiscard]] std::shared_ptr<TraceListener> traceListener() const;
    void fireTraceError(std::string_view message, std::string_view error);
    void setTraceEnabled(bool enabled);
    [[nodiscard]] bool traceEnabled() const;
    void setTraceOutputHandler(TraceOutputHandler handler);
    void addTraceHandler(std::string_view name);
    void removeTraceHandler(std::string_view name);
    void clearTraceHandlers();
    [[nodiscard]] const std::unordered_set<std::string>& tracedHandlers() const;
    void setGlobalHandlerFinder(GlobalHandlerFinder finder);

    [[nodiscard]] int callStackDepth() const;
    [[nodiscard]] bool hasActiveCallStack() const;
    [[nodiscard]] Scope* currentScope();
    [[nodiscard]] const Scope* currentScope() const;
    [[nodiscard]] std::vector<CallStackFrame> callStack() const;
    [[nodiscard]] std::string formatCallStack() const;

    [[nodiscard]] bool isFlushingDeferredScriptInstanceCalls() const;
    void deferScriptInstanceCall(Datum instance, std::string method_name, std::vector<Datum> args);
    void deferTask(std::function<void()> task);
    void flushDeferredTasks();
    [[nodiscard]] bool isFlushingDeferredTasks() const;

    [[nodiscard]] std::optional<HandlerRef> findHandler(std::string_view handler_name);
    [[nodiscard]] std::optional<HandlerRef> findHandler(const chunks::ScriptChunk& script,
                                                        std::string_view handler_name) const;
    void invalidateHandlerCache();

    [[nodiscard]] Datum callHandler(std::string_view handler_name, const std::vector<Datum>& args = {});
    [[nodiscard]] Datum callHandler(std::string_view handler_name,
                                    const std::vector<Datum>& args,
                                    const Datum& receiver);
    [[nodiscard]] Datum callBuiltin(std::string_view handler_name, const std::vector<Datum>& args = {});
    [[nodiscard]] bool fireAlertHook(std::string_view error_message);
    [[nodiscard]] bool fireAlertHook(std::string_view error_type, std::string_view error_message);
    [[nodiscard]] Datum executeHandler(const chunks::ScriptChunk& script,
                                       const chunks::ScriptChunk::Handler& handler,
                                       const std::vector<Datum>& args = {},
                                       const Datum& receiver = Datum::voidValue());
    [[nodiscard]] Datum executeHandler(const HandlerRef& handler,
                                       const std::vector<Datum>& args = {},
                                       const Datum& receiver = Datum::voidValue());
    [[nodiscard]] Datum executeHandler(const HandlerRef& handler,
                                       std::span<const Datum> args,
                                       const Datum& receiver = Datum::voidValue());

    [[nodiscard]] static std::string normalizeLookupName(std::string_view name);
    [[nodiscard]] static std::string formatTraceArgument(const Datum& value);
    [[nodiscard]] static bool isGlobalHandlerScriptType(chunks::ScriptChunkType script_type);
    [[nodiscard]] static bool shouldSkipDeconstructReentry(
        std::string_view normalized_handler_name,
        const Datum& effective_receiver,
        const chunks::ScriptChunk& current_script,
        std::string_view existing_normalized_handler_name,
        const Datum& existing_receiver,
        const chunks::ScriptChunk& existing_script);

private:
    [[nodiscard]] ExecutionContext::Callbacks baseCallbacksFor(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& file_owner,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner);
    [[nodiscard]] ExecutionContext::Callbacks cachedCallbacksFor(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& file_owner,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner);
    [[nodiscard]] std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesForScript(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    [[nodiscard]] std::string resolveName(
        const chunks::ScriptChunk& script,
        int name_id,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    [[nodiscard]] std::string handlerName(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    [[nodiscard]] Datum callAncestor(const std::vector<Datum>& args);
    [[nodiscard]] Datum findAncestorForCall(const Datum::ScriptInstanceRef& instance) const;
    [[nodiscard]] std::string scriptDisplayName(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr) const;
    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        std::span<const Datum> args,
        const Datum& receiver,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    [[nodiscard]] TraceListener::InstructionInfo buildInstructionInfo(
        const Scope& scope,
        const chunks::ScriptChunk::Instruction& instruction,
        bool include_snapshots) const;
    [[nodiscard]] std::unordered_map<std::string, Datum> captureLocals(const Scope& scope) const;
    [[nodiscard]] bool handlerDeclaresReceiverAsFirstParam(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    [[nodiscard]] bool skipDisabledTraceScriptPrologue(
        Scope& scope,
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& file_owner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner = nullptr) const;
    void executeInstruction(Scope& scope, ExecutionContext& context, bool trace_instruction);
    [[nodiscard]] std::int64_t currentTimeMillis() const;
    void traceRandomCall(int max, int result);
    void emitTracedHandlerCall(std::string_view handler_name,
                               const chunks::ScriptChunk& script,
                               std::span<const Datum> args);
    void traceOutput(const std::string& line) const;
    void emitConsoleHandlerEnter(const TraceListener::HandlerInfo& info);
    void emitConsoleHandlerExit(const TraceListener::HandlerInfo& info, const Datum& return_value);
    void emitConsoleInstruction(const TraceListener::InstructionInfo& info);
    void emitDebugMessage(std::string_view message) const;
    void resetConsoleTraceForHandler(int script_id);
    [[nodiscard]] bool shouldSuppressConsoleInstruction(int offset);
    [[nodiscard]] CallStackFrame toCallStackFrame(const Scope& scope) const;
    void registerRuntimeBuiltins();
    void flushDeferredScriptInstanceCalls();

    struct DeferredScriptInstanceCall {
        Datum instance{Datum::voidValue()};
        std::string method_name;
        std::vector<Datum> args;
    };

    struct HandlerMetadataKey {
        const chunks::ScriptChunk* script{nullptr};
        const chunks::ScriptChunk::Handler* handler{nullptr};
        const DirectorFile* file_owner{nullptr};
        const chunks::ScriptNamesChunk* script_names_owner{nullptr};

        bool operator==(const HandlerMetadataKey& other) const {
            return script == other.script &&
                   handler == other.handler &&
                   file_owner == other.file_owner &&
                   script_names_owner == other.script_names_owner;
        }
    };

    struct HandlerMetadataKeyHash {
        std::size_t operator()(const HandlerMetadataKey& key) const {
            return std::hash<const void*>{}(key.script) ^
                   (std::hash<const void*>{}(key.handler) << 1U) ^
                   (std::hash<const void*>{}(key.file_owner) << 2U) ^
                   (std::hash<const void*>{}(key.script_names_owner) << 3U);
        }
    };

    struct HandlerMetadata {
        std::string name;
        std::string normalized_name;
        bool first_param_declared_receiver{false};
        mutable int disabled_trace_script_prologue_length{-1};
    };

    struct ResolvedNameKey {
        const chunks::ScriptChunk* script{nullptr};
        const DirectorFile* file_owner{nullptr};
        const chunks::ScriptNamesChunk* script_names_owner{nullptr};
        int name_id{0};

        bool operator==(const ResolvedNameKey& other) const {
            return script == other.script &&
                   file_owner == other.file_owner &&
                   script_names_owner == other.script_names_owner &&
                   name_id == other.name_id;
        }
    };

    struct ResolvedNameKeyHash {
        std::size_t operator()(const ResolvedNameKey& key) const {
            return std::hash<const void*>{}(key.script) ^
                   (std::hash<const void*>{}(key.file_owner) << 1U) ^
                   (std::hash<const void*>{}(key.script_names_owner) << 2U) ^
                   (std::hash<int>{}(key.name_id) << 3U);
        }
    };

    struct CallbacksKey {
        const chunks::ScriptChunk* script{nullptr};
        const DirectorFile* file_owner{nullptr};
        const chunks::ScriptNamesChunk* script_names_owner{nullptr};

        bool operator==(const CallbacksKey& other) const {
            return script == other.script &&
                   file_owner == other.file_owner &&
                   script_names_owner == other.script_names_owner;
        }
    };

    struct CallbacksKeyHash {
        std::size_t operator()(const CallbacksKey& key) const {
            return std::hash<const void*>{}(key.script) ^
                   (std::hash<const void*>{}(key.file_owner) << 1U) ^
                   (std::hash<const void*>{}(key.script_names_owner) << 2U);
        }
    };

    DirectorFile* file_{nullptr};
    RuntimeGlobals globals_;
    std::map<std::string, Datum> prefs_;
    std::deque<Scope> call_stack_;
    std::deque<DeferredScriptInstanceCall> deferred_script_instance_calls_;
    std::deque<std::function<void()>> deferred_tasks_;
    builtin::BuiltinRegistry builtin_registry_;
    builtin::BuiltinContext builtin_context_;
    OpcodeRegistry opcode_registry_;
    std::shared_ptr<TraceListener> trace_listener_;
    TraceOutputHandler trace_output_handler_;
    GlobalHandlerFinder global_handler_finder_;
    std::unordered_set<std::string> traced_handlers_;
    std::unordered_map<std::string,
                       HandlerRef,
                       TransparentCaseInsensitiveStringHash,
                       TransparentCaseInsensitiveStringEqual> handler_cache_;
    std::unordered_set<std::string,
                       TransparentCaseInsensitiveStringHash,
                       TransparentCaseInsensitiveStringEqual> missing_handler_cache_;
    std::unordered_set<std::string> missing_builtin_debug_cache_;
    std::unordered_map<HandlerMetadataKey, std::shared_ptr<const HandlerMetadata>, HandlerMetadataKeyHash> handler_metadata_cache_;
    mutable std::unordered_map<ResolvedNameKey, std::string, ResolvedNameKeyHash> resolved_name_cache_;
    std::unordered_map<CallbacksKey, std::shared_ptr<ExecutionContext::Callbacks>, CallbacksKeyHash> callbacks_cache_;
    std::function<void()> pass_callback_;
    bool event_stopped_{false};
    bool in_error_state_{false};
    bool flushing_deferred_script_instance_calls_{false};
    bool flushing_deferred_tasks_{false};
    bool trace_enabled_{false};
    bool console_loop_suppressed_{false};
    bool explicit_random_seed_{false};
    AlertHookHandler alert_hook_handler_;
    int console_current_handler_id_{-1};
    int step_limit_{0};
    std::int64_t tick_deadline_ms_{0};
    std::int64_t tick_deadline_{0};
    std::int64_t handler_timeout_ms_{0};
    std::int64_t slow_handler_warning_threshold_ms_{1000};
    int random_seed_{0};
    std::int64_t random_state_{0};
    std::function<std::int64_t()> time_provider_;
    std::unordered_set<int> console_visited_offsets_;
    static std::function<void()> gc_callback_;
};

} // namespace libreshockwave::lingo::vm
