#include "libreshockwave/lingo/vm/lingo_vm.hpp"

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

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/lingo/opcode.hpp"
#include "libreshockwave/lingo/vm/debug_config.hpp"
#include "libreshockwave/lingo/vm/datum/datum_formatter.hpp"
#include "libreshockwave/lingo/vm/trace/console_trace_printer.hpp"
#include "libreshockwave/lingo/vm/trace/tracing_helper.hpp"
#include "libreshockwave/lingo/vm/util/ancestor_chain_walker.hpp"

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

std::int64_t frameDurationMsForTempo(int tempo) {
    return tempo > 0 ? std::max<std::int64_t>(1, 1000 / tempo) : 0;
}

std::int64_t frameDurationMsForMovie(const libreshockwave::player::MovieProperties* movie_properties) {
    if (movie_properties == nullptr) {
        return 0;
    }
    const Datum tempo = movie_properties->getMovieProp("tempo");
    if (const auto* value = tempo.asInt()) {
        return frameDurationMsForTempo(value->value);
    }
    return 0;
}

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

std::optional<std::string_view> directStringViewLikeJava(const Datum& datum) {
    if (const auto* value = datum.asString()) {
        return value->value;
    }
    if (const auto* value = datum.asFieldText()) {
        return value->value;
    }
    if (const auto* value = datum.asStringChunk()) {
        return value->value;
    }
    return std::nullopt;
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

std::optional<int> parseInitialRandomSeed(const char* raw_value) {
    if (raw_value == nullptr) {
        return std::nullopt;
    }

    std::string_view text(raw_value);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    const std::string seed_text(text);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(seed_text.c_str(), &end, 10);
    if (errno == ERANGE || end == seed_text.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::int64_t defaultSlowHandlerWarningThresholdMs() {
    const char* raw_value = std::getenv("LS_SLOW_HANDLER_WARNING_MS");
    if (raw_value == nullptr || *raw_value == '\0') {
        return SLOW_HANDLER_WARNING_MS;
    }

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(raw_value, &end, 10);
    if (errno == ERANGE || end == raw_value || *end != '\0' || parsed < 0) {
        return SLOW_HANDLER_WARNING_MS;
    }
    return parsed;
}

Datum scriptInstanceOrVoid(std::shared_ptr<Datum::ScriptInstanceRef> instance) {
    return instance ? Datum::scriptInstanceRef(std::move(instance)) : Datum::voidValue();
}

} // namespace

std::function<void()> LingoVM::gc_callback_;

LingoVM::LingoVM(DirectorFile* file)
    : file_(file) {
    setHandlerTimeoutMs(HANDLER_TIMEOUT_MS);
    setSlowHandlerWarningThresholdMs(defaultSlowHandlerWarningThresholdMs());
    setRandomSeed(0);
    if (const auto initial_random_seed = parseInitialRandomSeed(std::getenv("LS_INITIAL_RANDOM_SEED"))) {
        setRandomSeed(*initial_random_seed);
    }
    builtin_context_.value_evaluator = [this](const Datum& value) {
        const auto value_text = directStringViewLikeJava(value);
        if (!value_text.has_value()) {
            return Datum::voidValue();
        }
        const auto identifier = leadingValueIdentifier(*value_text);
        if (!identifier.has_value() || isValueLiteralKeyword(*identifier)) {
            return Datum::voidValue();
        }

        Datum global_value = getGlobal(*identifier);
        if (!global_value.isVoid()) {
            return global_value;
        }
        if (auto handler = findHandler(*identifier)) {
            return executeHandler(*handler);
        }
        return Datum::voidValue();
    };
    builtin_context_.random_int_handler = [this](int max) {
        return randomInt(max);
    };
    builtin_context_.debug_playback_enabled = DebugConfig::isDebugPlaybackEnabled();
    builtin_context_.get_pref_handler = [this](const std::string& name) {
        return getPref(name);
    };
    builtin_context_.set_pref_handler = [this](const std::string& name, const Datum& value) {
        return setPref(name, value);
    };
    builtin_context_.script_instance_method_deferrer = [this](const Datum& instance,
                                                          const std::string& method_name,
                                                          const std::vector<Datum>& args) {
        if (instance.type() != DatumType::ScriptInstanceRef || method_name.empty() ||
            isFlushingDeferredScriptInstanceCalls() || isFlushingDeferredTasks() || !hasActiveCallStack()) {
            return false;
        }

        deferTask([this, instance, method_name, args] {
            try {
                if (builtin_context_.call_target_handler) {
                    (void)builtin_context_.call_target_handler(instance, method_name, args);
                } else {
                    (void)callHandler(method_name, args, instance);
                }
            } catch (...) {
                // Java's deferred task path suppresses individual script-instance failures.
            }
        });
        return true;
    };
    builtin_context_.ancestor_call_handler = [this](const std::vector<Datum>& args) {
        return callAncestor(args);
    };
    registerRuntimeBuiltins();
}

DirectorFile* LingoVM::file() const { return file_; }
builtin::BuiltinRegistry& LingoVM::builtinRegistry() { return builtin_registry_; }
const builtin::BuiltinRegistry& LingoVM::builtinRegistry() const { return builtin_registry_; }
builtin::BuiltinContext& LingoVM::builtinContext() { return builtin_context_; }
const builtin::BuiltinContext& LingoVM::builtinContext() const { return builtin_context_; }
OpcodeRegistry& LingoVM::opcodeRegistry() { return opcode_registry_; }
const OpcodeRegistry& LingoVM::opcodeRegistry() const { return opcode_registry_; }

Datum LingoVM::getGlobal(std::string_view name) const {
    const auto found = globals_.find(name);
    return found == globals_.end() ? Datum::voidValue() : found->second;
}

void LingoVM::setGlobal(std::string name, Datum value) {
    globals_[std::move(name)] = std::move(value);
}

const RuntimeGlobals& LingoVM::globals() const {
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
    random_seed_ = seed;
    explicit_random_seed_ = true;
    random_state_ = (static_cast<std::int64_t>(seed) ^ RANDOM_MULTIPLIER) & RANDOM_MASK;
}

int LingoVM::randomSeed() const {
    return random_seed_;
}

int LingoVM::randomInt(int max) {
    int final_result = 1;
    if (max > 0) {
        auto next_bits = [this](int bits) {
            random_state_ = (random_state_ * RANDOM_MULTIPLIER + RANDOM_ADDEND) & RANDOM_MASK;
            return static_cast<int>(random_state_ >> (48 - bits));
        };

        int result = 0;
        if ((max & -max) == max) {
            result = static_cast<int>((static_cast<std::int64_t>(max) * next_bits(31)) >> 31);
        } else {
            int bits = 0;
            do {
                bits = next_bits(31);
                result = bits % max;
            } while (bits - result + (max - 1) < 0);
        }
        final_result = result + 1;
    } else {
        final_result = 1;
    }
    traceRandomCall(max, final_result);
    return final_result;
}

void LingoVM::setStepLimit(int limit) {
    step_limit_ = std::max(0, limit);
}

int LingoVM::stepLimit() const {
    return step_limit_;
}

void LingoVM::setTickDeadlineMs(std::int64_t milliseconds) {
    tick_deadline_ms_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::tickDeadlineMs() const {
    return tick_deadline_ms_;
}

void LingoVM::setTickDeadline(std::int64_t deadline_millis) {
    tick_deadline_ = std::max<std::int64_t>(0, deadline_millis);
}

std::int64_t LingoVM::tickDeadline() const {
    return tick_deadline_;
}

void LingoVM::setHandlerTimeoutMs(std::int64_t milliseconds) {
    handler_timeout_ms_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::handlerTimeoutMs() const {
    return handler_timeout_ms_;
}

void LingoVM::setSlowHandlerWarningThresholdMs(std::int64_t milliseconds) {
    slow_handler_warning_threshold_ms_ = std::max<std::int64_t>(0, milliseconds);
}

std::int64_t LingoVM::slowHandlerWarningThresholdMs() const {
    return slow_handler_warning_threshold_ms_;
}

void LingoVM::armTickDeadline() {
    tick_deadline_ = tick_deadline_ms_ > 0 ? currentTimeMillis() + tick_deadline_ms_ : 0;
}

void LingoVM::setTimeProvider(std::function<std::int64_t()> provider) {
    time_provider_ = std::move(provider);
}

void LingoVM::setGcCallback(std::function<void()> callback) {
    gc_callback_ = std::move(callback);
}

void LingoVM::setPassCallback(std::function<void()> callback) {
    pass_callback_ = std::move(callback);
}

void LingoVM::clearPassCallback() {
    pass_callback_ = nullptr;
}

bool LingoVM::eventStopped() const {
    return event_stopped_;
}

void LingoVM::resetEventStopped() {
    event_stopped_ = false;
}

void LingoVM::setErrorState(bool error_state) {
    in_error_state_ = error_state;
}

bool LingoVM::isInErrorState() const {
    return in_error_state_;
}

void LingoVM::resetErrorState() {
    in_error_state_ = false;
}

void LingoVM::setErrorHandlerSkipCallback(AlertHookHandler::SkipCallback callback) {
    alert_hook_handler_.setErrorHandlerSkipCallback(std::move(callback));
}

void LingoVM::setTraceListener(std::shared_ptr<TraceListener> listener) {
    trace_listener_ = std::move(listener);
}

std::shared_ptr<TraceListener> LingoVM::traceListener() const {
    return trace_listener_;
}

void LingoVM::fireTraceError(std::string_view message, std::string_view error) {
    if (trace_listener_) {
        trace_listener_->onError(message, error);
    }
}

void LingoVM::setTraceEnabled(bool enabled) {
    trace_enabled_ = enabled;
}

bool LingoVM::traceEnabled() const {
    return trace_enabled_;
}

void LingoVM::setTraceOutputHandler(TraceOutputHandler handler) {
    trace_output_handler_ = std::move(handler);
}

void LingoVM::addTraceHandler(std::string_view name) {
    if (!name.empty()) {
        traced_handlers_.insert(normalizeLookupName(name));
    }
}

void LingoVM::removeTraceHandler(std::string_view name) {
    traced_handlers_.erase(normalizeLookupName(name));
}

void LingoVM::clearTraceHandlers() {
    traced_handlers_.clear();
}

const std::unordered_set<std::string>& LingoVM::tracedHandlers() const {
    return traced_handlers_;
}

void LingoVM::setGlobalHandlerFinder(GlobalHandlerFinder finder) {
    global_handler_finder_ = std::move(finder);
    invalidateHandlerCache();
}

int LingoVM::callStackDepth() const {
    return static_cast<int>(call_stack_.size());
}

bool LingoVM::hasActiveCallStack() const {
    return !call_stack_.empty();
}

Scope* LingoVM::currentScope() {
    return call_stack_.empty() ? nullptr : &call_stack_.back();
}

const Scope* LingoVM::currentScope() const {
    return call_stack_.empty() ? nullptr : &call_stack_.back();
}

Datum LingoVM::findAncestorForCall(const Datum::ScriptInstanceRef& instance) const {
    const Scope* scope = currentScope();
    if (scope == nullptr || scope->script() == nullptr || !builtin_context_.script_chunk_id_resolver) {
        return scriptInstanceOrVoid(instance.ancestor());
    }

    const int current_script_id = scope->script()->id().value();
    if (const auto* script_node = util::findScriptNode(instance,
                                                       current_script_id,
                                                       builtin_context_.script_chunk_id_resolver)) {
        return scriptInstanceOrVoid(script_node->ancestor());
    }

    return scriptInstanceOrVoid(instance.ancestor());
}

Datum LingoVM::callAncestor(const std::vector<Datum>& args) {
    if (args.size() < 2 || args[1].type() != DatumType::ScriptInstanceRef || !builtin_context_.script_handler_finder) {
        return Datum::voidValue();
    }

    const std::string handler_name = args[0].asSymbol() != nullptr ? args[0].asSymbol()->name : args[0].stringValue();
    Datum ancestor = findAncestorForCall(args[1].scriptInstanceValue());
    if (ancestor.type() != DatumType::ScriptInstanceRef) {
        return Datum::voidValue();
    }

    const auto* current = &ancestor.scriptInstanceValue();
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& script_ref = current->scriptRef(); script_ref.has_value()) {
            const int cast_lib = script_ref->cast_lib > 0 ? script_ref->cast_lib : 1;
            const int member_num = script_ref->memberNum();
            auto location = builtin_context_.script_handler_finder(cast_lib, member_num, handler_name);
            if (location.has_value() && location->script != nullptr && location->handler != nullptr) {
                const std::span<const Datum> call_args(args.data() + 2, args.size() - 2);
                return executeHandler(
                    HandlerRef{
                        location->script,
                        location->handler,
                        location->script_owner,
                        location->file_owner,
                        location->script_names_owner,
                        location->script_type
                    },
                    call_args,
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
    frames.reserve(call_stack_.size());
    for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
        frames.push_back(toCallStackFrame(*it));
    }
    return frames;
}

std::string LingoVM::formatCallStack() const {
    if (call_stack_.empty()) {
        return "Lingo call stack: (empty)";
    }

    std::ostringstream out;
    out << "Lingo call stack:\n";
    for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
        const Scope& scope = *it;
        out << "  at "
            << (scope.script() != nullptr
                    ? handlerName(*scope.script(), scope.handler(), scope.fileOwner(), scope.scriptNamesOwner())
                    : std::string())
            << '(';
        const int display_arg_count = scope.displayArgumentCount();
        for (int index = 0; index < display_arg_count; ++index) {
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
    return flushing_deferred_script_instance_calls_;
}

void LingoVM::deferScriptInstanceCall(Datum instance, std::string method_name, std::vector<Datum> args) {
    if (instance.type() != DatumType::ScriptInstanceRef || method_name.empty()) {
        return;
    }
    deferred_script_instance_calls_.push_back(
        DeferredScriptInstanceCall{std::move(instance), std::move(method_name), std::move(args)});
}

void LingoVM::deferTask(std::function<void()> task) {
    if (!task) {
        return;
    }
    deferred_tasks_.push_back(std::move(task));
}

void LingoVM::flushDeferredTasks() {
    if (flushing_deferred_tasks_ || deferred_tasks_.empty() || !call_stack_.empty()) {
        return;
    }

    flushing_deferred_tasks_ = true;
    try {
        while (!deferred_tasks_.empty()) {
            auto task = std::move(deferred_tasks_.front());
            deferred_tasks_.pop_front();
            task();
            if (!call_stack_.empty()) {
                break;
            }
        }
    } catch (...) {
        flushing_deferred_tasks_ = false;
        throw;
    }
    flushing_deferred_tasks_ = false;
}

bool LingoVM::isFlushingDeferredTasks() const {
    return flushing_deferred_tasks_;
}

std::optional<HandlerRef> LingoVM::findHandler(std::string_view handler_name_value) {
    if (handler_name_value.empty()) {
        return std::nullopt;
    }

    if (const auto found = handler_cache_.find(handler_name_value); found != handler_cache_.end()) {
        return found->second;
    }
    if (missing_handler_cache_.contains(handler_name_value)) {
        return std::nullopt;
    }

    if (file_ != nullptr) {
        for (const auto& script : file_->scripts()) {
            if (!script || !isGlobalHandlerScriptType(script->resolvedScriptType())) {
                continue;
            }
            if (auto handler = findHandler(*script, handler_name_value)) {
                handler->script_owner = script;
                handler->script_type = script->resolvedScriptType();
                handler_cache_.emplace(std::string(handler_name_value), *handler);
                return handler;
            }
        }
    }

    if (global_handler_finder_) {
        if (auto handler = global_handler_finder_(handler_name_value); handler && handler->script != nullptr) {
            handler_cache_.emplace(std::string(handler_name_value), *handler);
            return handler;
        }
    }

    missing_handler_cache_.insert(std::string(handler_name_value));
    return std::nullopt;
}

std::optional<HandlerRef> LingoVM::findHandler(const chunks::ScriptChunk& script,
                                               std::string_view handler_name_value) const {
    const auto names = scriptNamesForScript(script);
    if (auto found = script.findHandlerPtr(handler_name_value, names.get())) {
        auto ref = HandlerRef{&script, found};
        ref.script_type = script.file() == file_ ? script.resolvedScriptType() : script.scriptType();
        return ref;
    }
    if (names != nullptr) {
        return std::nullopt;
    }

    for (const auto& handler : script.handlers()) {
        if (equalsIgnoreCase(script.getHandlerName(handler, nullptr), handler_name_value) ||
            equalsIgnoreCase("#" + std::to_string(handler.name_id), handler_name_value)) {
            auto ref = HandlerRef{&script, &handler};
            ref.script_type = script.file() == file_ ? script.resolvedScriptType() : script.scriptType();
            return ref;
        }
    }
    return std::nullopt;
}

void LingoVM::invalidateHandlerCache() {
    handler_cache_.clear();
    missing_handler_cache_.clear();
    handler_metadata_cache_.clear();
    resolved_name_cache_.clear();
    builtin_context_.script_resolution_cache.clear();
    builtin_context_.script_property_names_cache.clear();
    builtin_context_.direct_script_instance_handler_cache.clear();
    builtin_context_.script_instance_handler_cache.clear();
}

Datum LingoVM::callHandler(std::string_view handler_name_value, const std::vector<Datum>& args) {
    if (auto handler = findHandler(handler_name_value)) {
        return executeHandler(*handler, args);
    }
    return callBuiltin(handler_name_value, args);
}

Datum LingoVM::callHandler(std::string_view handler_name_value,
                           const std::vector<Datum>& args,
                           const Datum& receiver) {
    if (auto handler = findHandler(handler_name_value)) {
        return executeHandler(*handler, args, receiver);
    }
    return Datum::voidValue();
}

Datum LingoVM::callBuiltin(std::string_view handler_name_value, const std::vector<Datum>& args) {
    if (auto result = builtin_registry_.invokeIfPresent(handler_name_value, builtin_context_, args)) {
        return std::move(*result);
    }
    if (!handler_name_value.empty() &&
        (builtin_context_.debug_playback_enabled || DebugConfig::isDebugPlaybackEnabled())) {
        const auto normalized_name = normalizeLookupName(handler_name_value);
        if (missing_builtin_debug_cache_.insert(normalized_name).second) {
            emitDebugMessage("Unsupported Lingo global/builtin: " + std::string(handler_name_value));
        }
    }
    return Datum::voidValue();
}

bool LingoVM::fireAlertHook(std::string_view error_message) {
    return fireAlertHook("Alert", error_message);
}

bool LingoVM::fireAlertHook(std::string_view error_type, std::string_view error_message) {
    return alert_hook_handler_.fireAlertHook(error_type, error_message, builtin_context_.alert_hook_handler);
}

Datum LingoVM::executeHandler(const chunks::ScriptChunk& script,
                              const chunks::ScriptChunk::Handler& handler,
                              const std::vector<Datum>& args,
                              const Datum& receiver) {
    return executeHandler(HandlerRef{&script, &handler}, args, receiver);
}

Datum LingoVM::executeHandler(const HandlerRef& handler_ref,
                              const std::vector<Datum>& args,
                              const Datum& receiver) {
    return executeHandler(handler_ref, std::span<const Datum>(args), receiver);
}

Datum LingoVM::executeHandler(const HandlerRef& handler_ref,
                              std::span<const Datum> args,
                              const Datum& receiver) {
    if (in_error_state_) {
        return Datum::voidValue();
    }
    if (handler_ref.script == nullptr || handler_ref.handler == nullptr) {
        return Datum::voidValue();
    }

    const auto& script = *handler_ref.script;
    const auto& handler = *handler_ref.handler;
    const auto script_owner = handler_ref.script_owner;
    const auto file_owner = handler_ref.file_owner;
    const auto script_names_owner = handler_ref.script_names_owner;

    const HandlerMetadataKey metadata_key{&script, &handler, file_owner.get(), script_names_owner.get()};
    auto metadata_it = handler_metadata_cache_.find(metadata_key);
    if (metadata_it == handler_metadata_cache_.end()) {
        HandlerMetadata metadata;
        metadata.name = handlerName(script, handler, file_owner, script_names_owner);
        metadata.normalized_name = normalizeLookupName(metadata.name);
        metadata.first_param_declared_receiver =
            handlerDeclaresReceiverAsFirstParam(script, handler, file_owner, script_names_owner);
        metadata_it = handler_metadata_cache_.emplace(metadata_key, std::make_shared<HandlerMetadata>(std::move(metadata))).first;
    }
    const auto metadata = metadata_it->second;
    const std::string& current_handler_name = metadata->name;
    const std::string& normalized_handler_name = metadata->normalized_name;
    const bool is_alert_hook_handler = alert_hook_handler_.isErrorHandler(current_handler_name);
    if (alert_hook_handler_.shouldSkipErrorHandler(current_handler_name, args)) {
        return Datum::voidValue();
    }

    Datum scope_receiver = receiver;
    const Datum* effective_receiver = nullptr;
    if (!receiver.isVoid() && !receiver.isNull()) {
        effective_receiver = &receiver;
    } else if (handler.arg_name_ids.empty() &&
               !args.empty() &&
               args.front().type() == DatumType::ScriptInstanceRef) {
        // Unnamed handlers use Director's legacy first-argument receiver convention.
        scope_receiver = args.front();
        effective_receiver = &args.front();
    }
    if (normalized_handler_name == "deconstruct" && effective_receiver != nullptr) {
        for (const auto& existing : call_stack_) {
            const auto* existing_script = existing.script();
            if (existing_script != &script || existing.handler().name_id != handler.name_id) {
                continue;
            }
            if (shouldSkipDeconstructReentry(normalized_handler_name,
                                             *effective_receiver,
                                             script,
                                             normalized_handler_name,
                                             existing.receiver(),
                                             *existing_script)) {
                return Datum::voidValue();
            }
        }
    }

    if (call_stack_.size() >= MAX_CALL_STACK_DEPTH) {
        throw LingoException("Call stack overflow (max " + std::to_string(MAX_CALL_STACK_DEPTH) + " frames)");
    }
    if (traced_handlers_.contains(normalized_handler_name)) {
        emitTracedHandlerCall(current_handler_name, script, args);
    }

    const bool has_explicit_receiver = !receiver.isVoid() && !receiver.isNull();
    std::vector<Datum> effective_args =
        has_explicit_receiver ? std::vector<Datum>() : std::vector<Datum>(args.begin(), args.end());
    bool first_param_declared_receiver = false;
    if (has_explicit_receiver) {
        first_param_declared_receiver = metadata->first_param_declared_receiver;
        effective_args.reserve(args.size() + 1);
        effective_args.push_back(receiver);
        effective_args.insert(effective_args.end(), args.begin(), args.end());
    }

    if (call_stack_.empty()) {
        builtin_context_.script_instance_handler_cache.clear();
        builtin_context_.direct_script_instance_handler_cache.clear();
    }

    call_stack_.emplace_back(&script,
                            &handler,
                            std::move(effective_args),
                            scope_receiver,
                            first_param_declared_receiver,
                            script_owner,
                            file_owner,
                            script_names_owner);
    Scope& scope = call_stack_.back();
    const auto* previous_handler_args_view = builtin_context_.current_handler_args_view;
    builtin_context_.current_handler_args_view = &scope.arguments();
    Datum result = Datum::voidValue();
    std::optional<TraceListener::HandlerInfo> handler_info;
    bool alert_hook_depth_incremented = false;
    const bool emit_slow_handler_warnings = static_cast<bool>(builtin_context_.output_handler);
    const bool needs_safepoint_time = gc_callback_ || handler_timeout_ms_ > 0 || tick_deadline_ > 0;
    const std::int64_t handler_start_time =
        (emit_slow_handler_warnings || needs_safepoint_time) ? currentTimeMillis() : 0;
    int executed_steps = 0;
    int safepoint_countdown = SAFEPOINT_CHECK_INTERVAL;
    if (is_alert_hook_handler) {
        alert_hook_handler_.incrementDepth();
        alert_hook_depth_incremented = true;
    }
    auto leave_handler = [&] {
        if (emit_slow_handler_warnings) {
            const std::int64_t handler_elapsed_ms = currentTimeMillis() - handler_start_time;
            const std::int64_t frame_duration_ms = frameDurationMsForMovie(builtin_context_.movie_properties);
            const std::int64_t warning_threshold_ms = std::max(slow_handler_warning_threshold_ms_, frame_duration_ms);
            if (handler_elapsed_ms > warning_threshold_ms) {
                builtin_context_.output_handler(
                    "WARNING",
                    "handler " + current_handler_name + " took " + std::to_string(handler_elapsed_ms) +
                        "ms (" + std::to_string(executed_steps) + " instructions)");
            }
        }
        if (trace_listener_ && handler_info.has_value()) {
            trace_listener_->onHandlerExit(*handler_info, result);
        }
        if (trace_enabled_ && handler_info.has_value()) {
            emitConsoleHandlerExit(*handler_info, result);
        }
        if (traced_handlers_.contains(normalized_handler_name)) {
            traceOutput("[TRACE] " + current_handler_name + " returned " + formatTraceArgument(result));
        }
        if (alert_hook_depth_incremented) {
            alert_hook_handler_.decrementDepth();
            alert_hook_depth_incremented = false;
        }
        builtin_context_.current_handler_args_view = previous_handler_args_view;
        call_stack_.pop_back();
        flushDeferredScriptInstanceCalls();
    };
    try {
        const bool trace_handler = trace_enabled_ || (trace_listener_ && trace_listener_->needsHandlerTrace());
        if (trace_handler) {
            handler_info = buildHandlerInfo(script, handler, args, receiver, file_owner, script_names_owner);
            if (trace_enabled_) {
                emitConsoleHandlerEnter(*handler_info);
            }
        }
        if (trace_listener_ && trace_listener_->needsHandlerTrace() && handler_info.has_value()) {
            trace_listener_->onHandlerEnter(*handler_info);
        }
        (void)skipDisabledTraceScriptPrologue(scope, script, file_owner, script_names_owner);
        if (const auto* first = scope.currentInstruction()) {
            auto callbacks = cachedCallbacksFor(script, file_owner, script_names_owner);
            const bool trace_instruction =
                trace_enabled_ || (trace_listener_ != nullptr && trace_listener_->needsInstructionTrace());
            ExecutionContext context(scope,
                                     *first,
                                     &builtin_registry_,
                                     &builtin_context_,
                                     std::move(callbacks),
                                     variableMultiplierForScript(script),
                                     trace_instruction);
            const std::int64_t start_time = needs_safepoint_time ? handler_start_time : 0;
            std::int64_t last_gc_time = start_time;
            while (scope.hasMoreInstructions() && !scope.returned()) {
                ++executed_steps;
                if (step_limit_ > 0 && executed_steps > step_limit_) {
                    throw LingoException("Step limit exceeded (" + std::to_string(step_limit_) +
                                         " instructions) in handler '" + current_handler_name + "'");
                }
                if (needs_safepoint_time && --safepoint_countdown == 0) {
                    safepoint_countdown = SAFEPOINT_CHECK_INTERVAL;
                    const std::int64_t now = currentTimeMillis();
                    if (now - last_gc_time >= GC_SAFEPOINT_INTERVAL_MS) {
                        if (gc_callback_) {
                            gc_callback_();
                        }
                        last_gc_time = now;
                    }
                    if (handler_timeout_ms_ > 0 && now - start_time > handler_timeout_ms_) {
                        throw LingoException("Handler timeout (" + std::to_string(handler_timeout_ms_ / 1000) +
                                             "s, " + std::to_string(executed_steps) +
                                             " instructions) in handler '" + current_handler_name + "'");
                    }
                    if (tick_deadline_ > 0 && now > tick_deadline_) {
                        throw LingoException("Tick deadline exceeded in handler '" + current_handler_name +
                                             "' (" + std::to_string(executed_steps) + " instructions)");
                    }
                }
                executeInstruction(scope, context, trace_instruction);
            }
        }
        result = scope.takeReturnValue();
    } catch (const std::exception& error) {
        fireTraceError("Error in " + current_handler_name, error.what());
        if (!is_alert_hook_handler && fireAlertHook("Script Error", error.what())) {
            result = Datum::voidValue();
            leave_handler();
            return Datum::voidValue();
        }
        leave_handler();
        throw;
    } catch (...) {
        fireTraceError("Error in " + current_handler_name, "Unknown script error");
        if (!is_alert_hook_handler && fireAlertHook("Script Error", "Unknown script error")) {
            result = Datum::voidValue();
            leave_handler();
            return Datum::voidValue();
        }
        leave_handler();
        throw;
    }
    leave_handler();
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

bool LingoVM::isGlobalHandlerScriptType(chunks::ScriptChunkType script_type) {
    return script_type == chunks::ScriptChunkType::MovieScript ||
           script_type == chunks::ScriptChunkType::Parent;
}

bool LingoVM::shouldSkipDeconstructReentry(std::string_view normalized_handler_name,
                                           const Datum& effective_receiver,
                                           const chunks::ScriptChunk& current_script,
                                           std::string_view existing_normalized_handler_name,
                                           const Datum& existing_receiver,
                                           const chunks::ScriptChunk& existing_script) {
    return normalized_handler_name == "deconstruct" &&
           existing_normalized_handler_name == "deconstruct" &&
           effective_receiver.type() == DatumType::ScriptInstanceRef &&
           existing_receiver.type() == DatumType::ScriptInstanceRef &&
           effective_receiver == existing_receiver &&
           &current_script == &existing_script;
}

ExecutionContext::Callbacks LingoVM::baseCallbacksFor(
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) {
    ExecutionContext::Callbacks callbacks;
    callbacks.name_resolver = [this, &script, file_owner, script_names_owner](int name_id) {
        return resolveName(script, name_id, file_owner, script_names_owner);
    };
    callbacks.handler_finder = [this](std::string_view name) {
        return findHandler(name);
    };
    callbacks.handler_ref_span_executor = [this](const HandlerRef& handler,
                                              std::span<const Datum> args,
                                              const Datum& receiver) {
        return executeHandler(handler, args, receiver);
    };
    callbacks.global_getter = [this](std::string_view name) {
        return getGlobal(name);
    };
    callbacks.global_setter = [this](std::string_view name, Datum value) {
        setGlobal(std::string(name), std::move(value));
    };
    callbacks.builtin_invoker = [this](std::string_view name, const std::vector<Datum>& args) {
        return callBuiltin(name, args);
    };
    callbacks.error_state_setter = [this](bool error_state) {
        setErrorState(error_state);
    };
    callbacks.call_stack_formatter = [this] {
        return formatCallStack();
    };
    return callbacks;
}

ExecutionContext::Callbacks LingoVM::cachedCallbacksFor(
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) {
    // The base callbacks only depend on the script and its owners, so build
    // them once per script and reuse them across every handler invocation of
    // that script instead of reconstructing the std::function set each time.
    const CallbacksKey key{&script, file_owner.get(), script_names_owner.get()};
    ExecutionContext::Callbacks callbacks;
    const auto cached = callbacks_cache_.find(key);
    if (cached != callbacks_cache_.end()) {
        callbacks = *cached->second;
    } else {
        auto built = std::make_shared<ExecutionContext::Callbacks>(
            baseCallbacksFor(script, file_owner, script_names_owner));
        callbacks_cache_.emplace(key, built);
        callbacks = *built;
    }
    const bool needs_variable_trace =
        (trace_listener_ != nullptr && trace_listener_->needsVariableTrace()) || !traced_handlers_.empty();
    if (!needs_variable_trace) {
        return callbacks;
    }
    callbacks.variable_set_listener = [this](std::string_view type, std::string_view name, const Datum& value) {
        if (trace_listener_ && trace_listener_->needsVariableTrace()) {
            trace_listener_->onVariableSet(type, name, value);
        }
        const Scope* scope = currentScope();
        if (scope == nullptr || scope->script() == nullptr) {
            return;
        }
        const std::string current_handler_name =
            handlerName(*scope->script(), scope->handler(), scope->fileOwner(), scope->scriptNamesOwner());
        if (!traced_handlers_.contains(normalizeLookupName(current_handler_name))) {
            return;
        }

        std::string display_name(name);
        const auto names = scriptNamesForScript(*scope->script(), scope->fileOwner(), scope->scriptNamesOwner());
        const auto& handler = scope->handler();
        if (type == "local") {
            if (const auto index = parseTraceSlotIndex(name, "local");
                index.has_value() && *index < static_cast<int>(handler.local_name_ids.size())) {
                display_name = scope->script()->resolveName(handler.local_name_ids[static_cast<std::size_t>(*index)],
                                                           names.get());
            }
        } else if (type == "param") {
            if (const auto index = parseTraceSlotIndex(name, "param");
                index.has_value() && *index < static_cast<int>(handler.arg_name_ids.size())) {
                display_name = scope->script()->resolveName(handler.arg_name_ids[static_cast<std::size_t>(*index)],
                                                           names.get());
            }
        }
        traceOutput("[TRACE] set " + std::string(type) + " " + display_name + "=" + formatTraceArgument(value));
    };
    return callbacks;
}

std::shared_ptr<const chunks::ScriptNamesChunk> LingoVM::scriptNamesForScript(
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    if (script_names_owner) {
        return script_names_owner;
    }

    auto find_in_file = [&script](DirectorFile* source) -> std::shared_ptr<const chunks::ScriptNamesChunk> {
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

    if (auto names = find_in_file(file_)) {
        return names;
    }
    if (file_owner) {
        if (auto names = find_in_file(const_cast<DirectorFile*>(file_owner.get()))) {
            return names;
        }
    }
    if (auto* owner = const_cast<DirectorFile*>(script.file()); owner != file_) {
        return find_in_file(owner);
    }
    return nullptr;
}

std::string LingoVM::resolveName(const chunks::ScriptChunk& script,
                                 int name_id,
                                 const std::shared_ptr<const DirectorFile>& file_owner,
                                 const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    const ResolvedNameKey key{&script, file_owner.get(), script_names_owner.get(), name_id};
    if (const auto found = resolved_name_cache_.find(key); found != resolved_name_cache_.end()) {
        return found->second;
    }
    const auto names = scriptNamesForScript(script, file_owner, script_names_owner);
    std::string resolved = script.resolveName(name_id, names.get());
    auto [inserted, _] = resolved_name_cache_.emplace(key, std::move(resolved));
    return inserted->second;
}

std::string LingoVM::handlerName(const chunks::ScriptChunk& script,
                                 const chunks::ScriptChunk::Handler& handler,
                                 const std::shared_ptr<const DirectorFile>& file_owner,
                                 const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    const auto names = scriptNamesForScript(script, file_owner, script_names_owner);
    return script.getHandlerName(handler, names.get());
}

std::string LingoVM::scriptDisplayName(const chunks::ScriptChunk& script,
                                       const std::shared_ptr<const DirectorFile>& file_owner) const {
    auto find_name_in_file = [&script](DirectorFile* source) -> std::string {
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
    if (auto name = find_name_in_file(file_); !name.empty()) {
        return name;
    }
    if (file_owner) {
        if (auto name = find_name_in_file(const_cast<DirectorFile*>(file_owner.get())); !name.empty()) {
            return name;
        }
    }
    if (auto* owner = const_cast<DirectorFile*>(script.file()); owner != file_) {
        if (auto name = find_name_in_file(owner); !name.empty()) {
            return name;
        }
    }
    return "script#" + std::to_string(script.id().value());
}

TraceListener::HandlerInfo LingoVM::buildHandlerInfo(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    std::span<const Datum> args,
    const Datum& receiver,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    const auto names = scriptNamesForScript(script, file_owner, script_names_owner);
    return trace::TracingHelper().buildHandlerInfo(
        script, handler, args, receiver, globals_, names.get(), scriptDisplayName(script, file_owner));
}

TraceListener::InstructionInfo LingoVM::buildInstructionInfo(
    const Scope& scope,
    const chunks::ScriptChunk::Instruction& instruction,
    bool include_snapshots) const {
    const auto* script = scope.script();
    const auto names = script != nullptr ? scriptNamesForScript(*script, scope.fileOwner(), scope.scriptNamesOwner())
                                        : nullptr;
    return trace::TracingHelper().buildInstructionInfo(
        scope, instruction, globals_, names.get(), include_snapshots);
}

std::unordered_map<std::string, Datum> LingoVM::captureLocals(const Scope& scope) const {
    const auto* script = scope.script();
    const auto names = script != nullptr ? scriptNamesForScript(*script, scope.fileOwner(), scope.scriptNamesOwner())
                                        : nullptr;
    return trace::TracingHelper().captureLocals(scope, names.get());
}

bool LingoVM::handlerDeclaresReceiverAsFirstParam(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    if (handler.arg_name_ids.empty()) {
        return false;
    }
    const auto first_param_name = resolveName(script, handler.arg_name_ids.front(), file_owner, script_names_owner);
    return equalsIgnoreCase(first_param_name, "me") || equalsIgnoreCase(first_param_name, "this");
}

bool LingoVM::skipDisabledTraceScriptPrologue(
    Scope& scope,
    const chunks::ScriptChunk& script,
    const std::shared_ptr<const DirectorFile>& file_owner,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names_owner) const {
    if (scope.bytecodeIndex() != 0 || trace_enabled_ || !traced_handlers_.empty() ||
        (trace_listener_ != nullptr &&
         (trace_listener_->needsInstructionTrace() || trace_listener_->needsVariableTrace())) ||
        builtin_context_.movie_properties == nullptr ||
        builtin_context_.movie_properties->getMovieProp("traceScript").boolValue()) {
        return false;
    }

    HandlerMetadataKey metadata_key{
        &script,
        &scope.handler(),
        file_owner.get(),
        script_names_owner.get()
    };
    auto metadata = handler_metadata_cache_.find(metadata_key);
    if (metadata != handler_metadata_cache_.end() && metadata->second->disabled_trace_script_prologue_length >= 0) {
        const int cached_length = metadata->second->disabled_trace_script_prologue_length;
        if (cached_length > 0) {
            scope.setBytecodeIndex(cached_length);
            return true;
        }
        return false;
    }

    auto cache_result = [&](int length) {
        if (metadata != handler_metadata_cache_.end()) {
            metadata->second->disabled_trace_script_prologue_length = length;
        }
        if (length > 0) {
            scope.setBytecodeIndex(length);
            return true;
        }
        return false;
    };

    const auto& handler = scope.handler();
    const auto& instructions = handler.instructions;
    constexpr int prologue_length = 13;
    if (static_cast<int>(instructions.size()) <= prologue_length) {
        return cache_result(0);
    }

    auto opcode_at = [&](int index, Opcode opcode) {
        return instructions[static_cast<std::size_t>(index)].opcode == opcode;
    };
    auto name_at = [&](int index, std::string_view name) {
        return equalsIgnoreCase(resolveName(script,
                                            instructions[static_cast<std::size_t>(index)].argument,
                                            file_owner,
                                            script_names_owner),
                                name);
    };

    if (!opcode_at(0, Opcode::GET_MOVIE_PROP) || !name_at(0, "traceScript") ||
        !opcode_at(1, Opcode::JMP_IF_Z) ||
        handler.getInstructionIndex(instructions[1].offset + instructions[1].argument) != 5 ||
        !opcode_at(2, Opcode::PUSH_ZERO) ||
        !opcode_at(3, Opcode::PUSH_ARG_LIST_NO_RET) || instructions[3].argument != 1 ||
        !opcode_at(4, Opcode::EXT_CALL) || !name_at(4, "return") ||
        !opcode_at(5, Opcode::PUSH_ZERO) ||
        !opcode_at(6, Opcode::SET_MOVIE_PROP) || !name_at(6, "traceScript") ||
        !opcode_at(7, Opcode::GET_TOP_LEVEL_PROP) || !name_at(7, "_movie") ||
        !opcode_at(8, Opcode::PUSH_ZERO) ||
        !opcode_at(9, Opcode::SET_OBJ_PROP) || !name_at(9, "traceScript") ||
        !opcode_at(10, Opcode::GET_TOP_LEVEL_PROP) || !name_at(10, "_player") ||
        !opcode_at(11, Opcode::PUSH_ZERO) ||
        !opcode_at(12, Opcode::SET_OBJ_PROP) || !name_at(12, "traceScript")) {
        return cache_result(0);
    }

    return cache_result(prologue_length);
}

void LingoVM::executeInstruction(Scope& scope, ExecutionContext& context, bool trace_instruction) {
    const auto* instruction = scope.currentInstruction();
    if (instruction == nullptr) {
        scope.setReturned(true);
        return;
    }

    if (trace_instruction) {
        const bool include_snapshots =
            trace_enabled_ || trace_listener_ == nullptr ||
            trace_listener_->needsFullInstructionInfo(instruction->offset);
        auto info = buildInstructionInfo(scope, *instruction, include_snapshots);
        if (trace_enabled_) {
            emitConsoleInstruction(info);
        }
        if (trace_listener_) {
            trace_listener_->onInstruction(info);
        }
    }

    // Cooperative pause: if the trace listener (debugger) suspended execution,
    // unwind the handler BEFORE executing this instruction so the breakpoint
    // lands on the correct offset.
    if (trace_listener_ && trace_listener_->isExecutionSuspended()) {
        scope.setReturned(true);
        return;
    }

    context.setInstruction(*instruction);
    if (opcode_registry_.isDefaultRawHandler(instruction->opcode)) {
        const int variable_multiplier = context.variableMultiplier();
        auto scaled_argument = [&] {
            if (variable_multiplier == 1) {
                return instruction->argument;
            }
            if (variable_multiplier == 8) {
                return instruction->argument / 8;
            }
            return instruction->argument / variable_multiplier;
        };
        switch (instruction->opcode) {
            case Opcode::GET_LOCAL:
                scope.pushLocal(scaled_argument());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::GET_PARAM:
                scope.pushParam(scaled_argument());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::SET_LOCAL:
                context.setLocal(scaled_argument(), context.pop());
                scope.advanceBytecodeIndex();
                return;
            case Opcode::SET_PARAM:
                context.setParam(scaled_argument(), context.pop());
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

    if (const auto handler = opcode_registry_.getRaw(instruction->opcode)) {
        const bool advance = handler(context);
        if (advance) {
            scope.advanceBytecodeIndex();
        }
        return;
    }

    const auto* handler = opcode_registry_.get(instruction->opcode);
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
    if (time_provider_) {
        return time_provider_();
    }
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void LingoVM::traceRandomCall(int max, int result) {
    if (!traced_handlers_.contains("random")) {
        return;
    }

    std::ostringstream out;
    out << "[TRACE] random(" << max << ")=" << result;
    if (explicit_random_seed_) {
        out << " seed=" << random_seed_;
    }
    if (const Scope* scope = currentScope(); scope != nullptr && scope->script() != nullptr) {
        out << " at " << handlerName(*scope->script(), scope->handler(), scope->fileOwner(), scope->scriptNamesOwner())
            << " in \"" << scriptDisplayName(*scope->script(), scope->fileOwner()) << "\"";
    }
    traceOutput(out.str());
}

void LingoVM::emitTracedHandlerCall(std::string_view handler_name_value,
                                    const chunks::ScriptChunk& script,
                                    std::span<const Datum> args) {
    std::ostringstream out;
    out << "[TRACE] " << handler_name_value << '(';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << formatTraceArgument(args[index]);
    }
    out << ')';
    const std::string script_name = scriptDisplayName(script);
    if (!script_name.empty()) {
        out << " in \"" << script_name << "\"";
    }
    traceOutput(out.str());
    traceOutput(formatCallStack());
}

void LingoVM::traceOutput(const std::string& line) const {
    if (trace_output_handler_) {
        trace_output_handler_(line);
        return;
    }
    std::cout << line << '\n';
}

void LingoVM::emitDebugMessage(std::string_view message) const {
    if (message.empty()) {
        return;
    }
    if (trace_listener_) {
        trace_listener_->onDebugMessage(message);
    }
    if (builtin_context_.output_handler) {
        builtin_context_.output_handler("DEBUG", std::string(message));
        return;
    }
    std::cout << "[DEBUG] " << message << '\n';
}

void LingoVM::emitConsoleHandlerEnter(const TraceListener::HandlerInfo& info) {
    resetConsoleTraceForHandler(info.script_id);
    traceOutput(trace::ConsoleTracePrinter::formatHandlerEnter(info));
}

void LingoVM::emitConsoleHandlerExit(const TraceListener::HandlerInfo& info, const Datum& return_value) {
    if (auto line = trace::ConsoleTracePrinter::formatHandlerExit(info, return_value)) {
        traceOutput(*line);
    }
}

void LingoVM::emitConsoleInstruction(const TraceListener::InstructionInfo& info) {
    if (shouldSuppressConsoleInstruction(info.offset)) {
        return;
    }
    traceOutput(trace::ConsoleTracePrinter::formatInstruction(info));
}

void LingoVM::resetConsoleTraceForHandler(int script_id) {
    if (script_id == console_current_handler_id_) {
        return;
    }
    console_visited_offsets_.clear();
    console_loop_suppressed_ = false;
    console_current_handler_id_ = script_id;
}

bool LingoVM::shouldSuppressConsoleInstruction(int offset) {
    if (console_visited_offsets_.contains(offset)) {
        if (!console_loop_suppressed_) {
            traceOutput("    ... [loop iterations suppressed] ...");
            console_loop_suppressed_ = true;
        }
        return true;
    }
    console_visited_offsets_.insert(offset);
    console_loop_suppressed_ = false;
    return false;
}

LingoVM::CallStackFrame LingoVM::toCallStackFrame(const Scope& scope) const {
    std::vector<std::string> args;
    const int display_arg_count = scope.displayArgumentCount();
    args.reserve(static_cast<std::size_t>(display_arg_count));
    for (int index = 0; index < display_arg_count; ++index) {
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
    builtin_registry_.registerBuiltin("pass", [this](builtin::BuiltinContext&, const std::vector<Datum>&) {
        if (pass_callback_) {
            pass_callback_();
        }
        return Datum::voidValue();
    });
    builtin_registry_.registerBuiltin("stopevent", [this](builtin::BuiltinContext&, const std::vector<Datum>&) {
        event_stopped_ = true;
        return Datum::voidValue();
    });
}

void LingoVM::flushDeferredScriptInstanceCalls() {
    if (flushing_deferred_script_instance_calls_ || deferred_script_instance_calls_.empty() || !call_stack_.empty()) {
        return;
    }

    flushing_deferred_script_instance_calls_ = true;
    while (!deferred_script_instance_calls_.empty()) {
        auto call = std::move(deferred_script_instance_calls_.front());
        deferred_script_instance_calls_.pop_front();

        try {
            if (builtin_context_.call_target_handler) {
                (void)builtin_context_.call_target_handler(call.instance, call.method_name, call.args);
            } else {
                (void)callHandler(call.method_name, call.args, call.instance);
            }
        } catch (...) {
            // Java's deferred script-instance dispatcher suppresses per-target failures.
        }
    }
    flushing_deferred_script_instance_calls_ = false;
}

} // namespace libreshockwave::lingo::vm
