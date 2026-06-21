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

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/builtin/BuiltinRegistry.hpp"
#include "libreshockwave/lingo/vm/AlertHookHandler.hpp"
#include "libreshockwave/lingo/vm/ExecutionContext.hpp"
#include "libreshockwave/lingo/vm/OpcodeRegistry.hpp"
#include "libreshockwave/lingo/vm/Scope.hpp"
#include "libreshockwave/lingo/vm/TraceListener.hpp"

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
    using GlobalHandlerFinder = std::function<std::optional<HandlerRef>(std::string_view handlerName)>;

    struct CallStackFrame {
        std::string handlerName;
        std::string scriptName;
        int bytecodeIndex{0};
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
    void setTickDeadline(std::int64_t deadlineMillis);
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

    void setErrorState(bool errorState);
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
    void deferScriptInstanceCall(Datum instance, std::string methodName, std::vector<Datum> args);
    void deferTask(std::function<void()> task);
    void flushDeferredTasks();
    [[nodiscard]] bool isFlushingDeferredTasks() const;

    [[nodiscard]] std::optional<HandlerRef> findHandler(std::string_view handlerName);
    [[nodiscard]] std::optional<HandlerRef> findHandler(const chunks::ScriptChunk& script,
                                                        std::string_view handlerName) const;
    void invalidateHandlerCache();

    [[nodiscard]] Datum callHandler(std::string_view handlerName, const std::vector<Datum>& args = {});
    [[nodiscard]] Datum callHandler(std::string_view handlerName,
                                    const std::vector<Datum>& args,
                                    const Datum& receiver);
    [[nodiscard]] Datum callBuiltin(std::string_view handlerName, const std::vector<Datum>& args = {});
    [[nodiscard]] bool fireAlertHook(std::string_view errorMessage);
    [[nodiscard]] bool fireAlertHook(std::string_view errorType, std::string_view errorMessage);
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
    [[nodiscard]] static bool isGlobalHandlerScriptType(chunks::ScriptChunkType scriptType);
    [[nodiscard]] static bool shouldSkipDeconstructReentry(
        std::string_view normalizedHandlerName,
        const Datum& effectiveReceiver,
        const chunks::ScriptChunk& currentScript,
        std::string_view existingNormalizedHandlerName,
        const Datum& existingReceiver,
        const chunks::ScriptChunk& existingScript);

private:
    [[nodiscard]] ExecutionContext::Callbacks callbacksFor(
        const chunks::ScriptChunk& script,
        std::shared_ptr<const DirectorFile> fileOwner = nullptr,
        std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner = nullptr);
    [[nodiscard]] std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesForScript(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    [[nodiscard]] std::string resolveName(
        const chunks::ScriptChunk& script,
        int nameId,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    [[nodiscard]] std::string handlerName(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    [[nodiscard]] Datum callAncestor(const std::vector<Datum>& args);
    [[nodiscard]] Datum findAncestorForCall(const Datum::ScriptInstanceRef& instance) const;
    [[nodiscard]] std::string scriptDisplayName(
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr) const;
    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::vector<Datum>& args,
        const Datum& receiver,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    [[nodiscard]] TraceListener::InstructionInfo buildInstructionInfo(
        const Scope& scope,
        const chunks::ScriptChunk::Instruction& instruction) const;
    [[nodiscard]] std::unordered_map<std::string, Datum> captureLocals(const Scope& scope) const;
    [[nodiscard]] bool handlerDeclaresMeAsFirstParam(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    [[nodiscard]] bool skipDisabledTraceScriptPrologue(
        Scope& scope,
        const chunks::ScriptChunk& script,
        const std::shared_ptr<const DirectorFile>& fileOwner = nullptr,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner = nullptr) const;
    void executeInstruction(Scope& scope, ExecutionContext& context, bool traceInstruction);
    [[nodiscard]] std::int64_t currentTimeMillis() const;
    void traceRandomCall(int max, int result);
    void emitTracedHandlerCall(std::string_view handlerName,
                               const chunks::ScriptChunk& script,
                               const std::vector<Datum>& args);
    void traceOutput(const std::string& line) const;
    void emitConsoleHandlerEnter(const TraceListener::HandlerInfo& info);
    void emitConsoleHandlerExit(const TraceListener::HandlerInfo& info, const Datum& returnValue);
    void emitConsoleInstruction(const TraceListener::InstructionInfo& info);
    void emitDebugMessage(std::string_view message) const;
    void resetConsoleTraceForHandler(int scriptId);
    [[nodiscard]] bool shouldSuppressConsoleInstruction(int offset);
    [[nodiscard]] CallStackFrame toCallStackFrame(const Scope& scope) const;
    void registerRuntimeBuiltins();
    void flushDeferredScriptInstanceCalls();

    struct DeferredScriptInstanceCall {
        Datum instance{Datum::voidValue()};
        std::string methodName;
        std::vector<Datum> args;
    };

    struct HandlerMetadataKey {
        const chunks::ScriptChunk* script{nullptr};
        const chunks::ScriptChunk::Handler* handler{nullptr};
        const DirectorFile* fileOwner{nullptr};
        const chunks::ScriptNamesChunk* scriptNamesOwner{nullptr};

        bool operator==(const HandlerMetadataKey& other) const {
            return script == other.script &&
                   handler == other.handler &&
                   fileOwner == other.fileOwner &&
                   scriptNamesOwner == other.scriptNamesOwner;
        }
    };

    struct HandlerMetadataKeyHash {
        std::size_t operator()(const HandlerMetadataKey& key) const {
            return std::hash<const void*>{}(key.script) ^
                   (std::hash<const void*>{}(key.handler) << 1U) ^
                   (std::hash<const void*>{}(key.fileOwner) << 2U) ^
                   (std::hash<const void*>{}(key.scriptNamesOwner) << 3U);
        }
    };

    struct HandlerMetadata {
        std::string name;
        std::string normalizedName;
        bool firstParamDeclaredMe{false};
        mutable int disabledTraceScriptPrologueLength{-1};
    };

    struct ResolvedNameKey {
        const chunks::ScriptChunk* script{nullptr};
        const DirectorFile* fileOwner{nullptr};
        const chunks::ScriptNamesChunk* scriptNamesOwner{nullptr};
        int nameId{0};

        bool operator==(const ResolvedNameKey& other) const {
            return script == other.script &&
                   fileOwner == other.fileOwner &&
                   scriptNamesOwner == other.scriptNamesOwner &&
                   nameId == other.nameId;
        }
    };

    struct ResolvedNameKeyHash {
        std::size_t operator()(const ResolvedNameKey& key) const {
            return std::hash<const void*>{}(key.script) ^
                   (std::hash<const void*>{}(key.fileOwner) << 1U) ^
                   (std::hash<const void*>{}(key.scriptNamesOwner) << 2U) ^
                   (std::hash<int>{}(key.nameId) << 3U);
        }
    };

    DirectorFile* file_{nullptr};
    RuntimeGlobals globals_;
    std::map<std::string, Datum> prefs_;
    std::deque<Scope> callStack_;
    std::deque<DeferredScriptInstanceCall> deferredScriptInstanceCalls_;
    std::deque<std::function<void()>> deferredTasks_;
    builtin::BuiltinRegistry builtinRegistry_;
    builtin::BuiltinContext builtinContext_;
    OpcodeRegistry opcodeRegistry_;
    std::shared_ptr<TraceListener> traceListener_;
    TraceOutputHandler traceOutputHandler_;
    GlobalHandlerFinder globalHandlerFinder_;
    std::unordered_set<std::string> tracedHandlers_;
    std::unordered_map<std::string,
                       HandlerRef,
                       TransparentCaseInsensitiveStringHash,
                       TransparentCaseInsensitiveStringEqual> handlerCache_;
    std::unordered_set<std::string,
                       TransparentCaseInsensitiveStringHash,
                       TransparentCaseInsensitiveStringEqual> missingHandlerCache_;
    std::unordered_set<std::string> missingBuiltinDebugCache_;
    std::unordered_map<HandlerMetadataKey, std::shared_ptr<const HandlerMetadata>, HandlerMetadataKeyHash> handlerMetadataCache_;
    mutable std::unordered_map<ResolvedNameKey, std::string, ResolvedNameKeyHash> resolvedNameCache_;
    std::function<void()> passCallback_;
    bool eventStopped_{false};
    bool inErrorState_{false};
    bool flushingDeferredScriptInstanceCalls_{false};
    bool flushingDeferredTasks_{false};
    bool traceEnabled_{false};
    bool consoleLoopSuppressed_{false};
    bool explicitRandomSeed_{false};
    AlertHookHandler alertHookHandler_;
    int consoleCurrentHandlerId_{-1};
    int stepLimit_{0};
    std::int64_t tickDeadlineMs_{0};
    std::int64_t tickDeadline_{0};
    std::int64_t handlerTimeoutMs_{0};
    std::int64_t slowHandlerWarningThresholdMs_{1000};
    int randomSeed_{0};
    std::int64_t randomState_{0};
    std::function<std::int64_t()> timeProvider_;
    std::unordered_set<int> consoleVisitedOffsets_;
    static std::function<void()> gcCallback_;
};

} // namespace libreshockwave::lingo::vm
