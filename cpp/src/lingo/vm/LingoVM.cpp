#include "libreshockwave/lingo/vm/LingoVM.hpp"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/lingo/Opcode.hpp"
#include "libreshockwave/lingo/vm/DebugConfig.hpp"
#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"
#include "libreshockwave/lingo/vm/trace/ConsoleTracePrinter.hpp"
#include "libreshockwave/lingo/vm/trace/TracingHelper.hpp"
#include "libreshockwave/lingo/vm/util/AncestorChainWalker.hpp"

namespace libreshockwave::lingo::vm {
namespace {

constexpr int MAX_CALL_STACK_DEPTH = 50;
constexpr int SAFEPOINT_CHECK_INTERVAL = 0x10000;
constexpr std::int64_t GC_SAFEPOINT_INTERVAL_MS = 1000;
constexpr std::int64_t HANDLER_TIMEOUT_MS = 180000;
constexpr std::int64_t SLOW_HANDLER_WARNING_MS = 1000;
constexpr std::int64_t RANDOM_MULTIPLIER = 0x5DEECE66DLL;
constexpr std::int64_t RANDOM_ADDEND = 0xBLL;
constexpr std::int64_t RANDOM_MASK = (1LL << 48) - 1;

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::optional<int> parseTraceSlotIndex(std::string_view name, std::string_view prefix) {
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    int value = 0;
    const auto begin = name.data() + static_cast<std::ptrdiff_t>(prefix.size());
    const auto end = name.data() + static_cast<std::ptrdiff_t>(name.size());
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

int variableMultiplierForScript(const chunks::ScriptChunk& script) {
    const auto* file = script.file();
    if (file == nullptr || file->isCapitalX()) {
        return 1;
    }
    return file->version() >= 500 ? 8 : 6;
}

std::string_view trimView(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::string> leadingValueIdentifier(std::string_view value) {
    const std::string_view text = trimView(value);
    if (text.empty()) {
        return std::nullopt;
    }
    const auto first = static_cast<unsigned char>(text.front());
    if (!std::isalpha(first) && text.front() != '_') {
        return std::nullopt;
    }

    std::size_t end = 1;
    while (end < text.size()) {
        const auto ch = static_cast<unsigned char>(text[end]);
        if (!std::isalnum(ch) && text[end] != '_') {
            break;
        }
        ++end;
    }
    if (end < text.size() && text[end] == '(') {
        return std::nullopt;
    }
    return std::string(text.substr(0, end));
}

bool isValueLiteralKeyword(std::string_view identifier) {
    return equalsIgnoreCase(identifier, "TRUE") ||
           equalsIgnoreCase(identifier, "FALSE") ||
           equalsIgnoreCase(identifier, "VOID") ||
           equalsIgnoreCase(identifier, "EMPTY");
}

std::optional<int> parseInitialRandomSeed(const char* rawValue) {
    if (rawValue == nullptr) {
        return std::nullopt;
    }

    std::string_view text(rawValue);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    const std::string seedText(text);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(seedText.c_str(), &end, 10);
    if (errno == ERANGE || end == seedText.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::int64_t defaultSlowHandlerWarningThresholdMs() {
    const char* rawValue = std::getenv("LS_SLOW_HANDLER_WARNING_MS");
    if (rawValue == nullptr || *rawValue == '\0') {
        return SLOW_HANDLER_WARNING_MS;
    }

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(rawValue, &end, 10);
    if (errno == ERANGE || end == rawValue || *end != '\0' || parsed < 0) {
        return SLOW_HANDLER_WARNING_MS;
    }
    return parsed;
}

Datum scriptInstanceOrVoid(std::shared_ptr<Datum::ScriptInstanceRef> instance) {
    return instance ? Datum::scriptInstanceRef(std::move(instance)) : Datum::voidValue();
}

} // namespace

std::function<void()> LingoVM::gcCallback_;

LingoVM::LingoVM(DirectorFile* file)
    : file_(file) {
    setHandlerTimeoutMs(HANDLER_TIMEOUT_MS);
    setSlowHandlerWarningThresholdMs(defaultSlowHandlerWarningThresholdMs());
    setRandomSeed(0);
    if (const auto initialRandomSeed = parseInitialRandomSeed(std::getenv("LS_INITIAL_RANDOM_SEED"))) {
        setRandomSeed(*initialRandomSeed);
    }
    builtinContext_.valueEvaluator = [this](const Datum& value) {
        if (!value.isString()) {
            return Datum::voidValue();
        }
        const auto identifier = leadingValueIdentifier(value.stringValue());
        if (!identifier.has_value() || isValueLiteralKeyword(*identifier)) {
            return Datum::voidValue();
        }

        Datum globalValue = getGlobal(*identifier);
        if (!globalValue.isVoid()) {
            return globalValue;
        }
        if (auto handler = findHandler(*identifier)) {
            return executeHandler(*handler);
        }
        return Datum::voidValue();
    };
    builtinContext_.randomIntHandler = [this](int max) {
        return randomInt(max);
    };
    builtinContext_.debugPlaybackEnabled = DebugConfig::isDebugPlaybackEnabled();
    builtinContext_.getPrefHandler = [this](const std::string& name) {
        return getPref(name);
    };
    builtinContext_.setPrefHandler = [this](const std::string& name, const Datum& value) {
        return setPref(name, value);
    };
    builtinContext_.scriptInstanceMethodDeferrer = [this](const Datum& instance,
                                                          const std::string& methodName,
                                                          const std::vector<Datum>& args) {
        if (instance.type() != DatumType::ScriptInstanceRef || methodName.empty() ||
            isFlushingDeferredScriptInstanceCalls() || isFlushingDeferredTasks() || !hasActiveCallStack()) {
            return false;
        }

        deferTask([this, instance, methodName, args] {
            try {
                if (builtinContext_.callTargetHandler) {
                    (void)builtinContext_.callTargetHandler(instance, methodName, args);
                } else {
                    (void)callHandler(methodName, args, instance);
                }
            } catch (...) {
                // Java's deferred task path suppresses individual script-instance failures.
            }
        });
        return true;
    };
    builtinContext_.ancestorCallHandler = [this](const std::vector<Datum>& args) {
        return callAncestor(args);
    };
    registerRuntimeBuiltins();
}

DirectorFile* LingoVM::file() const { return file_; }
builtin::BuiltinRegistry& LingoVM::builtinRegistry() { return builtinRegistry_; }
const builtin::BuiltinRegistry& LingoVM::builtinRegistry() const { return builtinRegistry_; }
builtin::BuiltinContext& LingoVM::builtinContext() { return builtinContext_; }
const builtin::BuiltinContext& LingoVM::builtinContext() const { return builtinContext_; }
OpcodeRegistry& LingoVM::opcodeRegistry() { return opcodeRegistry_; }
const OpcodeRegistry& LingoVM::opcodeRegistry() const { return opcodeRegistry_; }

Datum LingoVM::getGlobal(std::string_view name) const {
    const auto found = globals_.find(std::string(name));
    return found == globals_.end() ? Datum::voidValue() : found->second;
}

void LingoVM::setGlobal(std::string name, Datum value) {
    globals_[std::move(name)] = std::move(value);
}

const std::unordered_map<std::string, Datum>& LingoVM::globals() const {
    return globals_;
}

void LingoVM::clearGlobals() {
    globals_.clear();
}

Datum LingoVM::getPref(std::string_view name) const {
    const auto found = prefs_.find(lower(name));
    return found == prefs_.end() ? Datum::voidValue() : found->second;
}

Datum LingoVM::setPref(std::string name, const Datum& value) {
    if (name.empty()) {
        return Datum::voidValue();
    }
    Datum stored = Datum::of(value.stringValue());
    prefs_[lower(name)] = stored;
    return stored;
}

const std::map<std::string, Datum>& LingoVM::prefs() const {
    return prefs_;
}

void LingoVM::setRandomSeed(int seed) {
    randomSeed_ = seed;
    explicitRandomSeed_ = true;
    randomState_ = (static_cast<std::int64_t>(seed) ^ RANDOM_MULTIPLIER) & RANDOM_MASK;
}

int LingoVM::randomSeed() const {
    return randomSeed_;
}

int LingoVM::randomInt(int max) {
    int finalResult = 1;
    if (max > 0) {
        auto nextBits = [this](int bits) {
            randomState_ = (randomState_ * RANDOM_MULTIPLIER + RANDOM_ADDEND) & RANDOM_MASK;
            return static_cast<int>(randomState_ >> (48 - bits));
        };

        int result = 0;
        if ((max & -max) == max) {
            result = static_cast<int>((static_cast<std::int64_t>(max) * nextBits(31)) >> 31);
        } else {
            int bits = 0;
            do {
                bits = nextBits(31);
                result = bits % max;
            } while (bits - result + (max - 1) < 0);
        }
        finalResult = result + 1;
    } else {
        finalResult = 1;
    }
    traceRandomCall(max, finalResult);
    return finalResult;
}

void LingoVM::setStepLimit(int limit) {
    stepLimit_ = std::max(0, limit);
}

int LingoVM::stepLimit() const {
    return stepLimit_;
}

void LingoVM::setTickDeadlineMs(std::int64_t milliseconds) {
    tickDeadlineMs_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::tickDeadlineMs() const {
    return tickDeadlineMs_;
}

void LingoVM::setTickDeadline(std::int64_t deadlineMillis) {
    tickDeadline_ = std::max<std::int64_t>(0, deadlineMillis);
}

std::int64_t LingoVM::tickDeadline() const {
    return tickDeadline_;
}

void LingoVM::setHandlerTimeoutMs(std::int64_t milliseconds) {
    handlerTimeoutMs_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::handlerTimeoutMs() const {
    return handlerTimeoutMs_;
}

void LingoVM::setSlowHandlerWarningThresholdMs(std::int64_t milliseconds) {
    slowHandlerWarningThresholdMs_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::slowHandlerWarningThresholdMs() const {
    return slowHandlerWarningThresholdMs_;
}

void LingoVM::armTickDeadline() {
    tickDeadline_ = tickDeadlineMs_ > 0 ? currentTimeMillis() + tickDeadlineMs_ : 0;
}

void LingoVM::setTimeProvider(std::function<std::int64_t()> provider) {
    timeProvider_ = std::move(provider);
}

void LingoVM::setGcCallback(std::function<void()> callback) {
    gcCallback_ = std::move(callback);
}

void LingoVM::setPassCallback(std::function<void()> callback) {
    passCallback_ = std::move(callback);
}

void LingoVM::clearPassCallback() {
    passCallback_ = nullptr;
}

bool LingoVM::eventStopped() const {
    return eventStopped_;
}

void LingoVM::resetEventStopped() {
    eventStopped_ = false;
}

void LingoVM::setErrorState(bool errorState) {
    inErrorState_ = errorState;
}

bool LingoVM::isInErrorState() const {
    return inErrorState_;
}

void LingoVM::resetErrorState() {
    inErrorState_ = false;
}

void LingoVM::setErrorHandlerSkipCallback(AlertHookHandler::SkipCallback callback) {
    alertHookHandler_.setErrorHandlerSkipCallback(std::move(callback));
}

void LingoVM::setTraceListener(std::shared_ptr<TraceListener> listener) {
    traceListener_ = std::move(listener);
}

std::shared_ptr<TraceListener> LingoVM::traceListener() const {
    return traceListener_;
}

void LingoVM::fireTraceError(std::string_view message, std::string_view error) {
    if (traceListener_) {
        traceListener_->onError(message, error);
    }
}

void LingoVM::setTraceEnabled(bool enabled) {
    traceEnabled_ = enabled;
}

bool LingoVM::traceEnabled() const {
    return traceEnabled_;
}

void LingoVM::setTraceOutputHandler(TraceOutputHandler handler) {
    traceOutputHandler_ = std::move(handler);
}

void LingoVM::addTraceHandler(std::string_view name) {
    if (!name.empty()) {
        tracedHandlers_.insert(normalizeLookupName(name));
    }
}

void LingoVM::removeTraceHandler(std::string_view name) {
    tracedHandlers_.erase(normalizeLookupName(name));
}

void LingoVM::clearTraceHandlers() {
    tracedHandlers_.clear();
}

const std::unordered_set<std::string>& LingoVM::tracedHandlers() const {
    return tracedHandlers_;
}

void LingoVM::setGlobalHandlerFinder(GlobalHandlerFinder finder) {
    globalHandlerFinder_ = std::move(finder);
    invalidateHandlerCache();
}

int LingoVM::callStackDepth() const {
    return static_cast<int>(callStack_.size());
}

bool LingoVM::hasActiveCallStack() const {
    return !callStack_.empty();
}

Scope* LingoVM::currentScope() {
    return callStack_.empty() ? nullptr : &callStack_.back();
}

const Scope* LingoVM::currentScope() const {
    return callStack_.empty() ? nullptr : &callStack_.back();
}

Datum LingoVM::findAncestorForCall(const Datum::ScriptInstanceRef& instance) const {
    const Scope* scope = currentScope();
    if (scope == nullptr || scope->script() == nullptr || !builtinContext_.scriptChunkIdResolver) {
        return scriptInstanceOrVoid(instance.ancestor());
    }

    const int currentScriptId = scope->script()->id().value();
    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& scriptRef = current->scriptRef(); scriptRef.has_value()) {
            const int castLib = scriptRef->castLib > 0 ? scriptRef->castLib : 1;
            const int scriptChunkId = builtinContext_.scriptChunkIdResolver(castLib, scriptRef->memberNum());
            if (scriptChunkId == currentScriptId) {
                return scriptInstanceOrVoid(current->ancestor());
            }
        }

        auto ancestor = current->ancestor();
        current = ancestor ? ancestor.get() : nullptr;
    }

    return scriptInstanceOrVoid(instance.ancestor());
}

Datum LingoVM::callAncestor(const std::vector<Datum>& args) {
    if (args.size() < 2 || args[1].type() != DatumType::ScriptInstanceRef || !builtinContext_.scriptHandlerFinder) {
        return Datum::voidValue();
    }

    const std::string handlerName = args[0].asSymbol() != nullptr ? args[0].asSymbol()->name : args[0].stringValue();
    Datum ancestor = findAncestorForCall(args[1].scriptInstanceValue());
    if (ancestor.type() != DatumType::ScriptInstanceRef) {
        return Datum::voidValue();
    }

    const auto* current = &ancestor.scriptInstanceValue();
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& scriptRef = current->scriptRef(); scriptRef.has_value()) {
            const int castLib = scriptRef->castLib > 0 ? scriptRef->castLib : 1;
            const int memberNum = scriptRef->memberNum();
            auto location = builtinContext_.scriptHandlerFinder(castLib, memberNum, handlerName);
            if (location.has_value() && location->script != nullptr && location->handler != nullptr) {
                std::vector<Datum> callArgs;
                callArgs.reserve(args.size() > 2 ? args.size() - 2 : 0);
                callArgs.insert(callArgs.end(), args.begin() + 2, args.end());
                return executeHandler(
                    HandlerRef{
                        location->script,
                        location->handler,
                        location->scriptOwner,
                        location->fileOwner,
                        location->scriptNamesOwner,
                        location->scriptType
                    },
                    callArgs,
                    args[1]);
            }
        }

        auto next = current->ancestor();
        current = next ? next.get() : nullptr;
    }

    return Datum::voidValue();
}

std::vector<LingoVM::CallStackFrame> LingoVM::callStack() const {
    std::vector<CallStackFrame> frames;
    frames.reserve(callStack_.size());
    for (auto it = callStack_.rbegin(); it != callStack_.rend(); ++it) {
        frames.push_back(toCallStackFrame(*it));
    }
    return frames;
}

std::string LingoVM::formatCallStack() const {
    if (callStack_.empty()) {
        return "Lingo call stack: (empty)";
    }

    std::ostringstream out;
    out << "Lingo call stack:\n";
    for (auto it = callStack_.rbegin(); it != callStack_.rend(); ++it) {
        const Scope& scope = *it;
        out << "  at "
            << (scope.script() != nullptr
                    ? handlerName(*scope.script(), scope.handler(), scope.fileOwner(), scope.scriptNamesOwner())
                    : std::string())
            << '(';
        const int displayArgCount = scope.displayArgumentCount();
        for (int index = 0; index < displayArgCount; ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << formatTraceArgument(scope.displayArgument(index));
        }
        out << ") (script#" << (scope.script() != nullptr ? scope.script()->id().value() : 0)
            << ") [bytecode " << scope.bytecodeIndex() << "]\n";
    }
    return out.str();
}

bool LingoVM::isFlushingDeferredScriptInstanceCalls() const {
    return flushingDeferredScriptInstanceCalls_;
}

void LingoVM::deferScriptInstanceCall(Datum instance, std::string methodName, std::vector<Datum> args) {
    if (instance.type() != DatumType::ScriptInstanceRef || methodName.empty()) {
        return;
    }
    deferredScriptInstanceCalls_.push_back(
        DeferredScriptInstanceCall{std::move(instance), std::move(methodName), std::move(args)});
}

void LingoVM::deferTask(std::function<void()> task) {
    if (!task) {
        return;
    }
    deferredTasks_.push_back(std::move(task));
}

void LingoVM::flushDeferredTasks() {
    if (flushingDeferredTasks_ || deferredTasks_.empty() || !callStack_.empty()) {
        return;
    }

    flushingDeferredTasks_ = true;
    try {
        while (!deferredTasks_.empty()) {
            auto task = std::move(deferredTasks_.front());
            deferredTasks_.pop_front();
            task();
            if (!callStack_.empty()) {
                break;
            }
        }
    } catch (...) {
        flushingDeferredTasks_ = false;
        throw;
    }
    flushingDeferredTasks_ = false;
}

bool LingoVM::isFlushingDeferredTasks() const {
    return flushingDeferredTasks_;
}

std::optional<HandlerRef> LingoVM::findHandler(std::string_view handlerNameValue) {
    if (handlerNameValue.empty()) {
        return std::nullopt;
    }

    if (const auto found = handlerCache_.find(handlerNameValue); found != handlerCache_.end()) {
        return found->second;
    }
    if (missingHandlerCache_.contains(handlerNameValue)) {
        return std::nullopt;
    }

    if (file_ != nullptr) {
        for (const auto& script : file_->scripts()) {
            if (!script || !isGlobalHandlerScriptType(script->resolvedScriptType())) {
                continue;
            }
            if (auto handler = findHandler(*script, handlerNameValue)) {
                handler->scriptOwner = script;
                handler->scriptType = script->resolvedScriptType();
                handlerCache_.emplace(std::string(handlerNameValue), *handler);
                return handler;
            }
        }
    }

    if (globalHandlerFinder_) {
        if (auto handler = globalHandlerFinder_(handlerNameValue); handler && handler->script != nullptr) {
            handlerCache_.emplace(std::string(handlerNameValue), *handler);
            return handler;
        }
    }

    missingHandlerCache_.insert(std::string(handlerNameValue));
    return std::nullopt;
}

std::optional<HandlerRef> LingoVM::findHandler(const chunks::ScriptChunk& script,
                                               std::string_view handlerNameValue) const {
    const auto names = scriptNamesForScript(script);
    if (auto found = script.findHandlerPtr(handlerNameValue, names.get())) {
        auto ref = HandlerRef{&script, found};
        ref.scriptType = script.file() == file_ ? script.resolvedScriptType() : script.scriptType();
        return ref;
    }
    if (names != nullptr) {
        return std::nullopt;
    }

    for (const auto& handler : script.handlers()) {
        if (equalsIgnoreCase(script.getHandlerName(handler, nullptr), handlerNameValue) ||
            equalsIgnoreCase("#" + std::to_string(handler.nameId), handlerNameValue)) {
            auto ref = HandlerRef{&script, &handler};
            ref.scriptType = script.file() == file_ ? script.resolvedScriptType() : script.scriptType();
            return ref;
        }
    }
    return std::nullopt;
}

void LingoVM::invalidateHandlerCache() {
    handlerCache_.clear();
    missingHandlerCache_.clear();
    handlerMetadataCache_.clear();
    resolvedNameCache_.clear();
    builtinContext_.scriptPropertyNamesCache.clear();
}

Datum LingoVM::callHandler(std::string_view handlerNameValue, const std::vector<Datum>& args) {
    if (auto handler = findHandler(handlerNameValue)) {
        return executeHandler(*handler, args);
    }
    return callBuiltin(handlerNameValue, args);
}

Datum LingoVM::callHandler(std::string_view handlerNameValue,
                           const std::vector<Datum>& args,
                           const Datum& receiver) {
    if (auto handler = findHandler(handlerNameValue)) {
        return executeHandler(*handler, args, receiver);
    }
    return Datum::voidValue();
}

Datum LingoVM::callBuiltin(std::string_view handlerNameValue, const std::vector<Datum>& args) {
    if (auto result = builtinRegistry_.invokeIfPresent(handlerNameValue, builtinContext_, args)) {
        return std::move(*result);
    }
    if (!handlerNameValue.empty() &&
        (builtinContext_.debugPlaybackEnabled || DebugConfig::isDebugPlaybackEnabled())) {
        const auto normalizedName = normalizeLookupName(handlerNameValue);
        if (missingBuiltinDebugCache_.insert(normalizedName).second) {
            emitDebugMessage("Unsupported Lingo global/builtin: " + std::string(handlerNameValue));
        }
    }
    return Datum::voidValue();
}

bool LingoVM::fireAlertHook(std::string_view errorMessage) {
    return fireAlertHook("Alert", errorMessage);
}

bool LingoVM::fireAlertHook(std::string_view errorType, std::string_view errorMessage) {
    return alertHookHandler_.fireAlertHook(errorType, errorMessage, builtinContext_.alertHookHandler);
}

Datum LingoVM::executeHandler(const chunks::ScriptChunk& script,
                              const chunks::ScriptChunk::Handler& handler,
                              const std::vector<Datum>& args,
                              const Datum& receiver) {
    return executeHandler(HandlerRef{&script, &handler}, args, receiver);
}

Datum LingoVM::executeHandler(const HandlerRef& handlerRef,
                              const std::vector<Datum>& args,
                              const Datum& receiver) {
    return executeHandler(handlerRef, std::span<const Datum>(args), receiver);
}

Datum LingoVM::executeHandler(const HandlerRef& handlerRef,
                              std::span<const Datum> args,
                              const Datum& receiver) {
    if (inErrorState_) {
        return Datum::voidValue();
    }
    if (handlerRef.script == nullptr || handlerRef.handler == nullptr) {
        return Datum::voidValue();
    }

    const auto& script = *handlerRef.script;
    const auto& handler = *handlerRef.handler;
    const auto scriptOwner = handlerRef.scriptOwner;
    const auto fileOwner = handlerRef.fileOwner;
    const auto scriptNamesOwner = handlerRef.scriptNamesOwner;

    const HandlerMetadataKey metadataKey{&script, &handler, fileOwner.get(), scriptNamesOwner.get()};
    auto metadataIt = handlerMetadataCache_.find(metadataKey);
    if (metadataIt == handlerMetadataCache_.end()) {
        HandlerMetadata metadata;
        metadata.name = handlerName(script, handler, fileOwner, scriptNamesOwner);
        metadata.normalizedName = normalizeLookupName(metadata.name);
        metadata.firstParamDeclaredMe = handlerDeclaresMeAsFirstParam(script, handler, fileOwner, scriptNamesOwner);
        metadataIt = handlerMetadataCache_.emplace(metadataKey, std::make_shared<HandlerMetadata>(std::move(metadata))).first;
    }
    const auto metadata = metadataIt->second;
    const std::string& currentHandlerName = metadata->name;
    const std::string& normalizedHandlerName = metadata->normalizedName;
    const bool isAlertHookHandler = alertHookHandler_.isErrorHandler(currentHandlerName);
    if (alertHookHandler_.shouldSkipErrorHandler(currentHandlerName, args)) {
        return Datum::voidValue();
    }

    Datum scopeReceiver = receiver;
    const Datum* effectiveReceiver = nullptr;
    if (!receiver.isVoid() && !receiver.isNull()) {
        effectiveReceiver = &receiver;
    } else if (!args.empty() && args.front().type() == DatumType::ScriptInstanceRef) {
        scopeReceiver = args.front();
        effectiveReceiver = &args.front();
    }
    if (normalizedHandlerName == "deconstruct" && effectiveReceiver != nullptr) {
        for (const auto& existing : callStack_) {
            const auto* existingScript = existing.script();
            if (existingScript != &script || existing.handler().nameId != handler.nameId) {
                continue;
            }
            if (shouldSkipDeconstructReentry(normalizedHandlerName,
                                             *effectiveReceiver,
                                             script,
                                             normalizedHandlerName,
                                             existing.receiver(),
                                             *existingScript)) {
                return Datum::voidValue();
            }
        }
    }

    if (callStack_.size() >= MAX_CALL_STACK_DEPTH) {
        throw LingoException("Call stack overflow (max " + std::to_string(MAX_CALL_STACK_DEPTH) + " frames)");
    }
    if (tracedHandlers_.contains(normalizedHandlerName)) {
        const std::vector<Datum> traceArgs(args.begin(), args.end());
        emitTracedHandlerCall(currentHandlerName, script, traceArgs);
    }

    std::vector<Datum> effectiveArgs;
    bool firstParamDeclaredMe = false;
    if (!receiver.isVoid() && !receiver.isNull()) {
        firstParamDeclaredMe = metadata->firstParamDeclaredMe;
        effectiveArgs.reserve(args.size() + 1);
        effectiveArgs.push_back(receiver);
        effectiveArgs.insert(effectiveArgs.end(), args.begin(), args.end());
    } else {
        effectiveArgs.assign(args.begin(), args.end());
    }

    if (callStack_.empty()) {
        builtinContext_.aliasRefreshRegistryIds.clear();
        builtinContext_.scriptInstanceHandlerCache.clear();
        builtinContext_.directScriptInstanceHandlerCache.clear();
    }

    callStack_.emplace_back(&script,
                            &handler,
                            std::move(effectiveArgs),
                            scopeReceiver,
                            firstParamDeclaredMe,
                            scriptOwner,
                            fileOwner,
                            scriptNamesOwner);
    Scope& scope = callStack_.back();
    const auto* previousHandlerArgsView = builtinContext_.currentHandlerArgsView;
    builtinContext_.currentHandlerArgsView = &scope.arguments();
    Datum result = Datum::voidValue();
    std::optional<TraceListener::HandlerInfo> handlerInfo;
    bool alertHookDepthIncremented = false;
    const bool emitSlowHandlerWarnings = static_cast<bool>(builtinContext_.outputHandler);
    const bool needsSafepointTime = gcCallback_ || handlerTimeoutMs_ > 0 || tickDeadline_ > 0;
    const std::int64_t handlerStartTime =
        (emitSlowHandlerWarnings || needsSafepointTime) ? currentTimeMillis() : 0;
    int executedSteps = 0;
    if (isAlertHookHandler) {
        alertHookHandler_.incrementDepth();
        alertHookDepthIncremented = true;
    }
    auto leaveHandler = [&] {
        if (emitSlowHandlerWarnings) {
            const std::int64_t handlerElapsedMs = currentTimeMillis() - handlerStartTime;
            if (handlerElapsedMs >= slowHandlerWarningThresholdMs_) {
                builtinContext_.outputHandler(
                    "WARNING",
                    "handler " + currentHandlerName + " took " + std::to_string(handlerElapsedMs) +
                        "ms (" + std::to_string(executedSteps) + " instructions)");
            }
        }
        if (traceListener_ && handlerInfo.has_value()) {
            traceListener_->onHandlerExit(*handlerInfo, result);
        }
        if (traceEnabled_ && handlerInfo.has_value()) {
            emitConsoleHandlerExit(*handlerInfo, result);
        }
        if (tracedHandlers_.contains(normalizedHandlerName)) {
            traceOutput("[TRACE] " + currentHandlerName + " returned " + formatTraceArgument(result));
        }
        if (alertHookDepthIncremented) {
            alertHookHandler_.decrementDepth();
            alertHookDepthIncremented = false;
        }
        builtinContext_.currentHandlerArgsView = previousHandlerArgsView;
        callStack_.pop_back();
        flushDeferredScriptInstanceCalls();
    };
    try {
        const bool traceHandler = traceEnabled_ || (traceListener_ && traceListener_->needsHandlerTrace());
        if (traceHandler) {
            const std::vector<Datum> traceArgs(args.begin(), args.end());
            handlerInfo = buildHandlerInfo(script, handler, traceArgs, receiver, fileOwner, scriptNamesOwner);
            if (traceEnabled_) {
                emitConsoleHandlerEnter(*handlerInfo);
            }
        }
        if (traceListener_ && traceListener_->needsHandlerTrace() && handlerInfo.has_value()) {
            traceListener_->onHandlerEnter(*handlerInfo);
        }
        (void)skipDisabledTraceScriptPrologue(scope, script, fileOwner, scriptNamesOwner);
        if (const auto* first = scope.currentInstruction()) {
            auto callbacks = callbacksFor(script, fileOwner, scriptNamesOwner);
            const bool traceInstruction =
                traceEnabled_ || (traceListener_ != nullptr && traceListener_->needsInstructionTrace());
            ExecutionContext context(scope,
                                     *first,
                                     &builtinRegistry_,
                                     &builtinContext_,
                                     std::move(callbacks),
                                     variableMultiplierForScript(script),
                                     traceInstruction);
            const std::int64_t startTime = needsSafepointTime ? handlerStartTime : 0;
            std::int64_t lastGcTime = startTime;
            while (scope.hasMoreInstructions() && !scope.returned()) {
                ++executedSteps;
                if (stepLimit_ > 0 && executedSteps > stepLimit_) {
                    throw LingoException("Step limit exceeded (" + std::to_string(stepLimit_) +
                                         " instructions) in handler '" + currentHandlerName + "'");
                }
                if ((executedSteps % SAFEPOINT_CHECK_INTERVAL) == 0) {
                    const std::int64_t now = currentTimeMillis();
                    if (now - lastGcTime >= GC_SAFEPOINT_INTERVAL_MS) {
                        if (gcCallback_) {
                            gcCallback_();
                        }
                        lastGcTime = now;
                    }
                    if (handlerTimeoutMs_ > 0 && now - startTime > handlerTimeoutMs_) {
                        throw LingoException("Handler timeout (" + std::to_string(handlerTimeoutMs_ / 1000) +
                                             "s, " + std::to_string(executedSteps) +
                                             " instructions) in handler '" + currentHandlerName + "'");
                    }
                    if (tickDeadline_ > 0 && now > tickDeadline_) {
                        throw LingoException("Tick deadline exceeded in handler '" + currentHandlerName +
                                             "' (" + std::to_string(executedSteps) + " instructions)");
                    }
                }
                executeInstruction(scope, context, traceInstruction);
            }
        }
        result = scope.returnValue();
    } catch (const std::exception& error) {
        fireTraceError("Error in " + currentHandlerName, error.what());
        if (!isAlertHookHandler && fireAlertHook("Script Error", error.what())) {
            result = Datum::voidValue();
            leaveHandler();
            return Datum::voidValue();
        }
        leaveHandler();
        throw;
    } catch (...) {
        fireTraceError("Error in " + currentHandlerName, "Unknown script error");
        if (!isAlertHookHandler && fireAlertHook("Script Error", "Unknown script error")) {
            result = Datum::voidValue();
            leaveHandler();
            return Datum::voidValue();
        }
        leaveHandler();
        throw;
    }
    leaveHandler();
    return result;
}

std::string LingoVM::normalizeLookupName(std::string_view name) {
    return lower(name);
}

std::string LingoVM::formatTraceArgument(const Datum& value) {
    if (value.isVoid()) {
        return "<VOID>";
    }
    if (const auto* symbol = value.asSymbol()) {
        return "#" + symbol->name;
    }
    if (const auto* string = value.asString()) {
        return "\"" + string->value + "\"";
    }
    return datum::format(value, 120);
}

bool LingoVM::isGlobalHandlerScriptType(chunks::ScriptChunkType scriptType) {
    return scriptType == chunks::ScriptChunkType::MovieScript ||
           scriptType == chunks::ScriptChunkType::Parent;
}

bool LingoVM::shouldSkipDeconstructReentry(std::string_view normalizedHandlerName,
                                           const Datum& effectiveReceiver,
                                           const chunks::ScriptChunk& currentScript,
                                           std::string_view existingNormalizedHandlerName,
                                           const Datum& existingReceiver,
                                           const chunks::ScriptChunk& existingScript) {
    return normalizedHandlerName == "deconstruct" &&
           existingNormalizedHandlerName == "deconstruct" &&
           effectiveReceiver.type() == DatumType::ScriptInstanceRef &&
           existingReceiver.type() == DatumType::ScriptInstanceRef &&
           effectiveReceiver == existingReceiver &&
           &currentScript == &existingScript;
}

ExecutionContext::Callbacks LingoVM::callbacksFor(
    const chunks::ScriptChunk& script,
    std::shared_ptr<const DirectorFile> fileOwner,
    std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner) {
    ExecutionContext::Callbacks callbacks;
    callbacks.nameResolver = [this, &script, fileOwner, scriptNamesOwner](int nameId) {
        return resolveName(script, nameId, fileOwner, scriptNamesOwner);
    };
    callbacks.handlerFinder = [this](std::string_view name) {
        return findHandler(name);
    };
    callbacks.handlerRefSpanExecutor = [this](const HandlerRef& handler,
                                              std::span<const Datum> args,
                                              const Datum& receiver) {
        return executeHandler(handler, args, receiver);
    };
    callbacks.globalGetter = [this](std::string_view name) {
        return getGlobal(name);
    };
    callbacks.globalSetter = [this](std::string_view name, Datum value) {
        setGlobal(std::string(name), std::move(value));
    };
    callbacks.builtinInvoker = [this](std::string_view name, const std::vector<Datum>& args) {
        return callBuiltin(name, args);
    };
    callbacks.errorStateSetter = [this](bool errorState) {
        setErrorState(errorState);
    };
    callbacks.callStackFormatter = [this] {
        return formatCallStack();
    };
    const bool needsVariableTrace =
        (traceListener_ != nullptr && traceListener_->needsVariableTrace()) || !tracedHandlers_.empty();
    if (!needsVariableTrace) {
        return callbacks;
    }
    callbacks.variableSetListener = [this](std::string_view type, std::string_view name, const Datum& value) {
        if (traceListener_ && traceListener_->needsVariableTrace()) {
            traceListener_->onVariableSet(type, name, value);
        }
        const Scope* scope = currentScope();
        if (scope == nullptr || scope->script() == nullptr) {
            return;
        }
        const std::string currentHandlerName =
            handlerName(*scope->script(), scope->handler(), scope->fileOwner(), scope->scriptNamesOwner());
        if (!tracedHandlers_.contains(normalizeLookupName(currentHandlerName))) {
            return;
        }

        std::string displayName(name);
        const auto names = scriptNamesForScript(*scope->script(), scope->fileOwner(), scope->scriptNamesOwner());
        const auto& handler = scope->handler();
        if (type == "local") {
            if (const auto index = parseTraceSlotIndex(name, "local");
                index.has_value() && *index < static_cast<int>(handler.localNameIds.size())) {
                displayName = scope->script()->resolveName(handler.localNameIds[static_cast<std::size_t>(*index)],
                                                           names.get());
            }
        } else if (type == "param") {
            if (const auto index = parseTraceSlotIndex(name, "param");
                index.has_value() && *index < static_cast<int>(handler.argNameIds.size())) {
                displayName = scope->script()->resolveName(handler.argNameIds[static_cast<std::size_t>(*index)],
                                                           names.get());
            }
        }
        traceOutput("[TRACE] set " + std::string(type) + " " + displayName + "=" + formatTraceArgument(value));
    };
    return callbacks;
}

std::shared_ptr<const chunks::ScriptNamesChunk> LingoVM::scriptNamesForScript(
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& fileOwner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    if (scriptNamesOwner) {
        return scriptNamesOwner;
    }

    auto findInFile = [&script](DirectorFile* source) -> std::shared_ptr<const chunks::ScriptNamesChunk> {
        if (source == nullptr) {
            return nullptr;
        }
        for (const auto& candidate : source->scripts()) {
            if (candidate.get() == &script) {
                return source->getScriptNamesForScript(candidate);
            }
        }
        for (const auto& candidate : source->scripts()) {
            if (candidate && candidate->id().value() == script.id().value()) {
                return source->getScriptNamesForScript(candidate);
            }
        }
        return nullptr;
    };

    if (auto names = findInFile(file_)) {
        return names;
    }
    if (fileOwner) {
        if (auto names = findInFile(const_cast<DirectorFile*>(fileOwner.get()))) {
            return names;
        }
    }
    if (auto* owner = const_cast<DirectorFile*>(script.file()); owner != file_) {
        return findInFile(owner);
    }
    return nullptr;
}

std::string LingoVM::resolveName(const chunks::ScriptChunk& script,
                                 int nameId,
                                 const std::shared_ptr<const DirectorFile>& fileOwner,
                                 const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    const ResolvedNameKey key{&script, fileOwner.get(), scriptNamesOwner.get(), nameId};
    if (const auto found = resolvedNameCache_.find(key); found != resolvedNameCache_.end()) {
        return found->second;
    }
    const auto names = scriptNamesForScript(script, fileOwner, scriptNamesOwner);
    std::string resolved = script.resolveName(nameId, names.get());
    auto [inserted, _] = resolvedNameCache_.emplace(key, std::move(resolved));
    return inserted->second;
}

std::string LingoVM::handlerName(const chunks::ScriptChunk& script,
                                 const chunks::ScriptChunk::Handler& handler,
                                 const std::shared_ptr<const DirectorFile>& fileOwner,
                                 const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    const auto names = scriptNamesForScript(script, fileOwner, scriptNamesOwner);
    return script.getHandlerName(handler, names.get());
}

std::string LingoVM::scriptDisplayName(const chunks::ScriptChunk& script,
                                       const std::shared_ptr<const DirectorFile>& fileOwner) const {
    auto findNameInFile = [&script](DirectorFile* source) -> std::string {
        if (source == nullptr) {
            return "";
        }
        for (const auto& candidate : source->scripts()) {
            if (candidate.get() == &script) {
                const std::string name = source->getScriptName(candidate);
                if (!name.empty()) {
                    return name;
                }
                break;
            }
        }
        return "";
    };
    if (auto name = findNameInFile(file_); !name.empty()) {
        return name;
    }
    if (fileOwner) {
        if (auto name = findNameInFile(const_cast<DirectorFile*>(fileOwner.get())); !name.empty()) {
            return name;
        }
    }
    if (auto* owner = const_cast<DirectorFile*>(script.file()); owner != file_) {
        if (auto name = findNameInFile(owner); !name.empty()) {
            return name;
        }
    }
    return "script#" + std::to_string(script.id().value());
}

TraceListener::HandlerInfo LingoVM::buildHandlerInfo(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    const std::vector<Datum>& args,
    const Datum& receiver,
    const std::shared_ptr<const DirectorFile>& fileOwner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    const auto names = scriptNamesForScript(script, fileOwner, scriptNamesOwner);
    return trace::TracingHelper().buildHandlerInfo(
        script, handler, args, receiver, globals_, names.get(), scriptDisplayName(script, fileOwner));
}

TraceListener::InstructionInfo LingoVM::buildInstructionInfo(
    const Scope& scope,
    const chunks::ScriptChunk::Instruction& instruction) const {
    const auto* script = scope.script();
    const auto names = script != nullptr ? scriptNamesForScript(*script, scope.fileOwner(), scope.scriptNamesOwner())
                                        : nullptr;
    return trace::TracingHelper().buildInstructionInfo(scope, instruction, globals_, names.get());
}

std::unordered_map<std::string, Datum> LingoVM::captureLocals(const Scope& scope) const {
    const auto* script = scope.script();
    const auto names = script != nullptr ? scriptNamesForScript(*script, scope.fileOwner(), scope.scriptNamesOwner())
                                        : nullptr;
    return trace::TracingHelper().captureLocals(scope, names.get());
}

bool LingoVM::handlerDeclaresMeAsFirstParam(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    const std::shared_ptr<const DirectorFile>& fileOwner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    if (handler.argNameIds.empty()) {
        return false;
    }
    return equalsIgnoreCase(resolveName(script, handler.argNameIds.front(), fileOwner, scriptNamesOwner), "me");
}

bool LingoVM::skipDisabledTraceScriptPrologue(
    Scope& scope,
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& fileOwner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner) const {
    if (scope.bytecodeIndex() != 0 || traceEnabled_ || !tracedHandlers_.empty() ||
        (traceListener_ != nullptr &&
         (traceListener_->needsInstructionTrace() || traceListener_->needsVariableTrace())) ||
        builtinContext_.movieProperties == nullptr ||
        builtinContext_.movieProperties->getMovieProp("traceScript").boolValue()) {
        return false;
    }

    HandlerMetadataKey metadataKey{
        &script,
        &scope.handler(),
        fileOwner.get(),
        scriptNamesOwner.get()
    };
    auto metadata = handlerMetadataCache_.find(metadataKey);
    if (metadata != handlerMetadataCache_.end() && metadata->second->disabledTraceScriptPrologueLength >= 0) {
        const int cachedLength = metadata->second->disabledTraceScriptPrologueLength;
        if (cachedLength > 0) {
            scope.setBytecodeIndex(cachedLength);
            return true;
        }
        return false;
    }

    auto cacheResult = [&](int length) {
        if (metadata != handlerMetadataCache_.end()) {
            metadata->second->disabledTraceScriptPrologueLength = length;
        }
        if (length > 0) {
            scope.setBytecodeIndex(length);
            return true;
        }
        return false;
    };

    const auto& handler = scope.handler();
    const auto& instructions = handler.instructions;
    constexpr int prologueLength = 13;
    if (static_cast<int>(instructions.size()) <= prologueLength) {
        return cacheResult(0);
    }

    auto opcodeAt = [&](int index, Opcode opcode) {
        return instructions[static_cast<std::size_t>(index)].opcode == opcode;
    };
    auto nameAt = [&](int index, std::string_view name) {
        return equalsIgnoreCase(resolveName(script,
                                            instructions[static_cast<std::size_t>(index)].argument,
                                            fileOwner,
                                            scriptNamesOwner),
                                name);
    };

    if (!opcodeAt(0, Opcode::GET_MOVIE_PROP) || !nameAt(0, "traceScript") ||
        !opcodeAt(1, Opcode::JMP_IF_Z) ||
        handler.getInstructionIndex(instructions[1].offset + instructions[1].argument) != 5 ||
        !opcodeAt(2, Opcode::PUSH_ZERO) ||
        !opcodeAt(3, Opcode::PUSH_ARG_LIST_NO_RET) || instructions[3].argument != 1 ||
        !opcodeAt(4, Opcode::EXT_CALL) || !nameAt(4, "return") ||
        !opcodeAt(5, Opcode::PUSH_ZERO) ||
        !opcodeAt(6, Opcode::SET_MOVIE_PROP) || !nameAt(6, "traceScript") ||
        !opcodeAt(7, Opcode::GET_TOP_LEVEL_PROP) || !nameAt(7, "_movie") ||
        !opcodeAt(8, Opcode::PUSH_ZERO) ||
        !opcodeAt(9, Opcode::SET_OBJ_PROP) || !nameAt(9, "traceScript") ||
        !opcodeAt(10, Opcode::GET_TOP_LEVEL_PROP) || !nameAt(10, "_player") ||
        !opcodeAt(11, Opcode::PUSH_ZERO) ||
        !opcodeAt(12, Opcode::SET_OBJ_PROP) || !nameAt(12, "traceScript")) {
        return cacheResult(0);
    }

    return cacheResult(prologueLength);
}

void LingoVM::executeInstruction(Scope& scope, ExecutionContext& context, bool traceInstruction) {
    const auto* instruction = scope.currentInstruction();
    if (instruction == nullptr) {
        scope.setReturned(true);
        return;
    }

    if (traceInstruction) {
        auto info = buildInstructionInfo(scope, *instruction);
        if (traceEnabled_) {
            emitConsoleInstruction(info);
        }
        if (traceListener_) {
            traceListener_->onInstruction(info);
        }
    }

    context.setInstruction(*instruction);
    if (opcodeRegistry_.isDefaultRawHandler(instruction->opcode)) {
        const int variableMultiplier = context.variableMultiplier();
        auto scaledArgument = [&] {
            if (variableMultiplier == 1) {
                return instruction->argument;
            }
            if (variableMultiplier == 8) {
                return instruction->argument / 8;
            }
            return instruction->argument / variableMultiplier;
        };
        switch (instruction->opcode) {
            case Opcode::GET_LOCAL:
                scope.pushLocal(scaledArgument());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::GET_PARAM:
                scope.pushParam(scaledArgument());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::SET_LOCAL:
                context.setLocal(scaledArgument(), context.pop());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::SET_PARAM:
                context.setParam(scaledArgument(), context.pop());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::PUSH_ZERO:
                scope.push(Datum::of(0));
                scope.advanceBytecodeIndex();
                return;
            case Opcode::PUSH_INT8:
            case Opcode::PUSH_INT16:
            case Opcode::PUSH_INT32:
                scope.push(Datum::of(instruction->argument));
                scope.advanceBytecodeIndex();
                return;
            default:
                break;
        }
    }

    if (const auto handler = opcodeRegistry_.getRaw(instruction->opcode)) {
        const bool advance = handler(context);
        if (advance) {
            scope.advanceBytecodeIndex();
        }
        return;
    }

    const auto* handler = opcodeRegistry_.get(instruction->opcode);
    if (handler == nullptr) {
        scope.advanceBytecodeIndex();
        return;
    }

    const bool advance = (*handler)(context);
    if (advance) {
        scope.advanceBytecodeIndex();
    }
}

std::int64_t LingoVM::currentTimeMillis() const {
    if (timeProvider_) {
        return timeProvider_();
    }
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void LingoVM::traceRandomCall(int max, int result) {
    if (!tracedHandlers_.contains("random")) {
        return;
    }

    std::ostringstream out;
    out << "[TRACE] random(" << max << ")=" << result;
    if (explicitRandomSeed_) {
        out << " seed=" << randomSeed_;
    }
    if (const Scope* scope = currentScope(); scope != nullptr && scope->script() != nullptr) {
        out << " at " << handlerName(*scope->script(), scope->handler(), scope->fileOwner(), scope->scriptNamesOwner())
            << " in \"" << scriptDisplayName(*scope->script(), scope->fileOwner()) << "\"";
    }
    traceOutput(out.str());
}

void LingoVM::emitTracedHandlerCall(std::string_view handlerNameValue,
                                    const chunks::ScriptChunk& script,
                                    const std::vector<Datum>& args) {
    std::ostringstream out;
    out << "[TRACE] " << handlerNameValue << '(';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << formatTraceArgument(args[index]);
    }
    out << ')';
    const std::string scriptName = scriptDisplayName(script);
    if (!scriptName.empty()) {
        out << " in \"" << scriptName << "\"";
    }
    traceOutput(out.str());
    traceOutput(formatCallStack());
}

void LingoVM::traceOutput(const std::string& line) const {
    if (traceOutputHandler_) {
        traceOutputHandler_(line);
        return;
    }
    std::cout << line << '\n';
}

void LingoVM::emitDebugMessage(std::string_view message) const {
    if (message.empty()) {
        return;
    }
    if (traceListener_) {
        traceListener_->onDebugMessage(message);
    }
    if (builtinContext_.outputHandler) {
        builtinContext_.outputHandler("DEBUG", std::string(message));
        return;
    }
    std::cout << "[DEBUG] " << message << '\n';
}

void LingoVM::emitConsoleHandlerEnter(const TraceListener::HandlerInfo& info) {
    resetConsoleTraceForHandler(info.scriptId);
    traceOutput(trace::ConsoleTracePrinter::formatHandlerEnter(info));
}

void LingoVM::emitConsoleHandlerExit(const TraceListener::HandlerInfo& info, const Datum& returnValue) {
    if (auto line = trace::ConsoleTracePrinter::formatHandlerExit(info, returnValue)) {
        traceOutput(*line);
    }
}

void LingoVM::emitConsoleInstruction(const TraceListener::InstructionInfo& info) {
    if (shouldSuppressConsoleInstruction(info.offset)) {
        return;
    }
    traceOutput(trace::ConsoleTracePrinter::formatInstruction(info));
}

void LingoVM::resetConsoleTraceForHandler(int scriptId) {
    if (scriptId == consoleCurrentHandlerId_) {
        return;
    }
    consoleVisitedOffsets_.clear();
    consoleLoopSuppressed_ = false;
    consoleCurrentHandlerId_ = scriptId;
}

bool LingoVM::shouldSuppressConsoleInstruction(int offset) {
    if (consoleVisitedOffsets_.contains(offset)) {
        if (!consoleLoopSuppressed_) {
            traceOutput("    ... [loop iterations suppressed] ...");
            consoleLoopSuppressed_ = true;
        }
        return true;
    }
    consoleVisitedOffsets_.insert(offset);
    consoleLoopSuppressed_ = false;
    return false;
}

LingoVM::CallStackFrame LingoVM::toCallStackFrame(const Scope& scope) const {
    std::vector<std::string> args;
    const int displayArgCount = scope.displayArgumentCount();
    args.reserve(static_cast<std::size_t>(displayArgCount));
    for (int index = 0; index < displayArgCount; ++index) {
        args.push_back(formatTraceArgument(scope.displayArgument(index)));
    }

    return CallStackFrame{
        scope.script() != nullptr
            ? handlerName(*scope.script(), scope.handler(), scope.fileOwner(), scope.scriptNamesOwner())
            : std::string(),
        "script#" + std::to_string(scope.script() != nullptr ? scope.script()->id().value() : 0),
        scope.bytecodeIndex(),
        std::move(args)
    };
}

void LingoVM::registerRuntimeBuiltins() {
    builtinRegistry_.registerBuiltin("pass", [this](builtin::BuiltinContext&, const std::vector<Datum>&) {
        if (passCallback_) {
            passCallback_();
        }
        return Datum::voidValue();
    });
    builtinRegistry_.registerBuiltin("stopevent", [this](builtin::BuiltinContext&, const std::vector<Datum>&) {
        eventStopped_ = true;
        return Datum::voidValue();
    });
}

void LingoVM::flushDeferredScriptInstanceCalls() {
    if (flushingDeferredScriptInstanceCalls_ || deferredScriptInstanceCalls_.empty() || !callStack_.empty()) {
        return;
    }

    flushingDeferredScriptInstanceCalls_ = true;
    while (!deferredScriptInstanceCalls_.empty()) {
        auto call = std::move(deferredScriptInstanceCalls_.front());
        deferredScriptInstanceCalls_.pop_front();

        try {
            if (builtinContext_.callTargetHandler) {
                (void)builtinContext_.callTargetHandler(call.instance, call.methodName, call.args);
            } else {
                (void)callHandler(call.methodName, call.args, call.instance);
            }
        } catch (...) {
            // Java's deferred script-instance dispatcher suppresses per-target failures.
        }
    }
    flushingDeferredScriptInstanceCalls_ = false;
}

} // namespace libreshockwave::lingo::vm
