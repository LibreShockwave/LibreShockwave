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

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/builtin/BuiltinRegistry.hpp"
#include "libreshockwave/lingo/vm/Scope.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::vm {

struct HandlerRef {
    HandlerRef() = default;
    HandlerRef(const chunks::ScriptChunk* scriptValue,
               const chunks::ScriptChunk::Handler* handlerValue,
               std::shared_ptr<const chunks::ScriptChunk> scriptOwnerValue = nullptr,
               std::shared_ptr<const DirectorFile> fileOwnerValue = nullptr,
               std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwnerValue = nullptr,
               chunks::ScriptChunkType scriptTypeValue = chunks::ScriptChunkType::Unknown)
        : script(scriptValue),
          handler(handlerValue),
          scriptOwner(std::move(scriptOwnerValue)),
          fileOwner(std::move(fileOwnerValue)),
          scriptNamesOwner(std::move(scriptNamesOwnerValue)),
          scriptType(scriptTypeValue) {}
    HandlerRef(const chunks::ScriptChunk* scriptValue,
               const chunks::ScriptChunk::Handler& handlerValue,
               std::shared_ptr<const chunks::ScriptChunk> scriptOwnerValue = nullptr,
               std::shared_ptr<const DirectorFile> fileOwnerValue = nullptr,
               std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwnerValue = nullptr,
               chunks::ScriptChunkType scriptTypeValue = chunks::ScriptChunkType::Unknown)
        : HandlerRef(scriptValue,
                     &handlerValue,
                     std::move(scriptOwnerValue),
                     std::move(fileOwnerValue),
                     std::move(scriptNamesOwnerValue),
                     scriptTypeValue) {}

    const chunks::ScriptChunk* script{nullptr};
    const chunks::ScriptChunk::Handler* handler{nullptr};
    std::shared_ptr<const chunks::ScriptChunk> scriptOwner;
    std::shared_ptr<const DirectorFile> fileOwner;
    std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner;
    chunks::ScriptChunkType scriptType{chunks::ScriptChunkType::Unknown};
};

class ExecutionContext {
public:
    using HandlerExecutor = std::function<Datum(const chunks::ScriptChunk& script,
                                                const chunks::ScriptChunk::Handler& handler,
                                                const std::vector<Datum>& args,
                                                const Datum& receiver)>;
    using HandlerRefExecutor = std::function<Datum(const HandlerRef& handler,
                                                   const std::vector<Datum>& args,
                                                   const Datum& receiver)>;
    using HandlerRefSpanExecutor = std::function<Datum(const HandlerRef& handler,
                                                       std::span<const Datum> args,
                                                       const Datum& receiver)>;
    using NameResolver = std::function<std::string(int nameId)>;
    using HandlerFinder = std::function<std::optional<HandlerRef>(std::string_view name)>;
    using GlobalGetter = std::function<Datum(std::string_view name)>;
    using GlobalSetter = std::function<void(std::string_view name, Datum value)>;
    using BuiltinInvoker = std::function<Datum(std::string_view name, const std::vector<Datum>& args)>;
    using ErrorStateSetter = std::function<void(bool errorState)>;
    using CallStackFormatter = std::function<std::string()>;
    using VariableSetListener = std::function<void(std::string_view type,
                                                   std::string_view name,
                                                   const Datum& value)>;

    struct Callbacks {
        HandlerExecutor handlerExecutor;
        HandlerRefExecutor handlerRefExecutor;
        HandlerRefSpanExecutor handlerRefSpanExecutor;
        NameResolver nameResolver;
        HandlerFinder handlerFinder;
        GlobalGetter globalGetter;
        GlobalSetter globalSetter;
        BuiltinInvoker builtinInvoker;
        ErrorStateSetter errorStateSetter;
        CallStackFormatter callStackFormatter;
        VariableSetListener variableSetListener;
    };

    ExecutionContext(Scope& scope,
                     chunks::ScriptChunk::Instruction instruction,
                     builtin::BuiltinRegistry* builtins = nullptr,
                     builtin::BuiltinContext* builtinContext = nullptr,
                     Callbacks callbacks = {},
                     int variableMultiplier = 1,
                     bool instructionTraceEnabled = false);

    void setInstruction(chunks::ScriptChunk::Instruction instruction) {
        instruction_ = instruction;
        argument_ = instruction.argument;
    }

    [[nodiscard]] Scope& scope() { return *scope_; }
    [[nodiscard]] const Scope& scope() const { return *scope_; }
    [[nodiscard]] const chunks::ScriptChunk::Instruction& instruction() const { return instruction_; }
    [[nodiscard]] int argument() const { return argument_; }
    [[nodiscard]] int scaledArgument() const {
        if (variableMultiplier_ == 1) {
            return argument_;
        }
        if (variableMultiplier_ == 8) {
            return argument_ / 8;
        }
        return argument_ / variableMultiplier_;
    }
    [[nodiscard]] int variableMultiplier() const { return variableMultiplier_; }
    [[nodiscard]] int instructionOffset() const { return instruction_.offset; }
    [[nodiscard]] bool instructionTraceEnabled() const { return instructionTraceEnabled_; }

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
    void setErrorState(bool errorState);
    [[nodiscard]] bool hasVariableSetListener() const { return static_cast<bool>(callbacks_.variableSetListener); }
    void tracePropertySet(std::string_view propName, const Datum& value) const;

    void jumpTo(int targetOffset);
    [[nodiscard]] const chunks::ScriptChunk::Handler* findLocalHandler(int index) const;
    [[nodiscard]] const std::vector<chunks::ScriptChunk::LiteralEntry>& literals() const;
    [[nodiscard]] const std::string& resolveNameRef(int nameId) const;
    [[nodiscard]] std::string resolveName(int nameId) const;
    [[nodiscard]] std::optional<HandlerRef> findHandler(std::string_view name) const;
    [[nodiscard]] Datum executeHandler(const chunks::ScriptChunk& script,
                                       const chunks::ScriptChunk::Handler& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const;
    [[nodiscard]] Datum executeHandler(const chunks::ScriptChunk& script,
                                       const chunks::ScriptChunk::Handler& handler,
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
    [[nodiscard]] builtin::BuiltinContext* builtinContext() { return builtinContext_; }
    [[nodiscard]] const builtin::BuiltinContext* builtinContext() const { return builtinContext_; }

    [[nodiscard]] std::string formatCallStack() const;
    [[nodiscard]] LingoException error(const std::string& message) const;

private:
    Scope* scope_;
    chunks::ScriptChunk::Instruction instruction_;
    int argument_;
    int variableMultiplier_;
    bool instructionTraceEnabled_;
    int cachedJumpOffset_ = -2147483648;
    int cachedJumpIndex_ = -1;
    builtin::BuiltinRegistry* builtins_;
    builtin::BuiltinContext* builtinContext_;
    Callbacks callbacks_;
    mutable std::unordered_map<int, std::string> resolvedNames_;
    mutable int cachedResolvedNameId_ = -2147483648;
    mutable const std::string* cachedResolvedName_ = nullptr;
};

} // namespace libreshockwave::lingo::vm
