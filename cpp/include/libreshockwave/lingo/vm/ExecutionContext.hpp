#pragma once

#include <functional>
#include <memory>
#include <optional>
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

    void setInstruction(chunks::ScriptChunk::Instruction instruction);

    [[nodiscard]] Scope& scope();
    [[nodiscard]] const Scope& scope() const;
    [[nodiscard]] const chunks::ScriptChunk::Instruction& instruction() const;
    [[nodiscard]] int argument() const;
    [[nodiscard]] int scaledArgument() const;
    [[nodiscard]] int variableMultiplier() const;
    [[nodiscard]] int instructionOffset() const;
    [[nodiscard]] bool instructionTraceEnabled() const;

    void push(Datum value);
    [[nodiscard]] Datum pop();
    [[nodiscard]] Datum peek() const;
    [[nodiscard]] Datum peek(int depth) const;
    [[nodiscard]] const Datum& peekRef(int depth = 0) const;
    void swap();
    [[nodiscard]] std::vector<Datum> popArgs(int count);

    [[nodiscard]] Datum getLocal(int index) const;
    void setLocal(int index, Datum value);
    [[nodiscard]] Datum getParam(int index) const;
    void setParam(int index, Datum value);

    [[nodiscard]] Datum getGlobal(std::string_view name) const;
    void setGlobal(std::string_view name, Datum value);

    void setReturnValue(Datum value);
    void setReturned(bool returned);
    void setErrorState(bool errorState);
    [[nodiscard]] bool hasVariableSetListener() const;
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
    [[nodiscard]] Datum executeHandler(const HandlerRef& handler,
                                       const std::vector<Datum>& args,
                                       const Datum& receiver) const;

    [[nodiscard]] bool isBuiltin(std::string_view name) const;
    [[nodiscard]] Datum invokeBuiltin(std::string_view name, const std::vector<Datum>& args);
    [[nodiscard]] std::optional<Datum> invokeBuiltinIfPresent(std::string_view name, const std::vector<Datum>& args);
    [[nodiscard]] builtin::BuiltinRegistry* builtins();
    [[nodiscard]] const builtin::BuiltinRegistry* builtins() const;
    [[nodiscard]] builtin::BuiltinContext* builtinContext();
    [[nodiscard]] const builtin::BuiltinContext* builtinContext() const;

    [[nodiscard]] std::string formatCallStack() const;
    [[nodiscard]] LingoException error(const std::string& message) const;

private:
    Scope* scope_;
    chunks::ScriptChunk::Instruction instruction_;
    int argument_;
    int scaledArgument_;
    int variableMultiplier_;
    bool instructionTraceEnabled_;
    int cachedJumpOffset_ = -2147483648;
    int cachedJumpIndex_ = -1;
    builtin::BuiltinRegistry* builtins_;
    builtin::BuiltinContext* builtinContext_;
    Callbacks callbacks_;
    mutable std::unordered_map<int, std::string> resolvedNames_;
};

} // namespace libreshockwave::lingo::vm
