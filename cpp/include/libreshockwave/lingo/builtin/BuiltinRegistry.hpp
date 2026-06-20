#pragma once

#include <cstdint>
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
#include "libreshockwave/lingo/vm/util/StringChunkUtils.hpp"
#include "libreshockwave/player/MovieProperties.hpp"
#include "libreshockwave/player/audio/SoundManager.hpp"
#include "libreshockwave/player/net/NetManager.hpp"
#include "libreshockwave/player/SpriteProperties.hpp"
#include "libreshockwave/player/timeout/TimeoutManager.hpp"

namespace libreshockwave::bitmap {
class Palette;
} // namespace libreshockwave::bitmap

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::builtin {

struct BuiltinContext {
    struct ResolvedPalette {
        std::shared_ptr<const bitmap::Palette> palette;
        std::optional<Datum::CastMemberRef> memberRef;
        std::optional<std::string> systemName;
    };

    struct ScriptHandlerLocation {
        ScriptHandlerLocation() = default;
        ScriptHandlerLocation(const chunks::ScriptChunk* scriptValue,
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
        ScriptHandlerLocation(const chunks::ScriptChunk* scriptValue,
                              const chunks::ScriptChunk::Handler& handlerValue,
                              std::shared_ptr<const chunks::ScriptChunk> scriptOwnerValue = nullptr,
                              std::shared_ptr<const DirectorFile> fileOwnerValue = nullptr,
                              std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwnerValue = nullptr,
                              chunks::ScriptChunkType scriptTypeValue = chunks::ScriptChunkType::Unknown)
            : ScriptHandlerLocation(scriptValue,
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

    using PuppetPaletteHandler = std::function<void(std::optional<Datum> paletteRef)>;
    using CastMemberCreator = std::function<Datum(int castLib, const std::string& memberType)>;
    using NamedCastMemberCreator = std::function<Datum(const std::string& memberName, const std::string& memberType)>;
    using CastLibNumberResolver = std::function<int(int castLib)>;
    using CastLibNameResolver = std::function<int(const std::string& name)>;
    using CastLibCountSupplier = std::function<int()>;
    using CastLibPropertyGetter = std::function<Datum(int castLib, const std::string& propertyName)>;
    using CastLibPropertySetter = std::function<bool(int castLib, const std::string& propertyName, const Datum& value)>;
    using CastMemberCountSupplier = std::function<int(int castLib)>;
    using CastMemberResolver = std::function<Datum(int castLib, int memberNum)>;
    using CastMemberNameResolver = std::function<Datum(int castLib, const std::string& memberName)>;
    using CastMemberExistsResolver = std::function<bool(int castLib, int memberNum)>;
    using RegistryVisibleMemberResolver = std::function<bool(int castLib, int memberNum)>;
    using CastMemberMethodHandler = std::function<Datum(int castLib,
                                                        int memberNum,
                                                        const std::string& methodName,
                                                        const std::vector<Datum>& args)>;
    using CastMemberPropertyGetter = std::function<Datum(int castLib, int memberNum, const std::string& propertyName)>;
    using CastMemberPropertySetter = std::function<bool(int castLib,
                                                        int memberNum,
                                                        const std::string& propertyName,
                                                        const Datum& value)>;
    using SpriteMethodHandler = std::function<Datum(int channel,
                                                    const std::string& methodName,
                                                    const std::vector<Datum>& args)>;
    using FieldResolver = std::function<Datum(const Datum& identifier, int castLib)>;
    using FieldParsedValueResolver = std::function<Datum(int castLib, int memberNum, std::uint64_t revision)>;
    using FieldSetter = std::function<void(const Datum& identifier, int castLib, const std::string& value)>;
    using XtraRegisteredResolver = std::function<bool(const std::string& xtraName)>;
    using XtraInstanceCreator = std::function<Datum(const std::string& xtraName, const std::vector<Datum>& args)>;
    using XtraHandler = std::function<Datum(const Datum::XtraInstance& instance,
                                            const std::string& handlerName,
                                            const std::vector<Datum>& args)>;
    using XtraPropertyGetter = std::function<Datum(const Datum::XtraInstance& instance, const std::string& propertyName)>;
    using XtraPropertySetter = std::function<void(const Datum::XtraInstance& instance,
                                                  const std::string& propertyName,
                                                  const Datum& value)>;
    using CallTargetHandler = std::function<Datum(const Datum& target,
                                                  const std::string& handlerName,
                                                  const std::vector<Datum>& args)>;
    using ScriptInstanceMethodDeferrer = std::function<bool(const Datum& instance,
                                                            const std::string& methodName,
                                                            const std::vector<Datum>& args)>;
    using NewInstanceHandler = std::function<Datum(const Datum& target, const std::vector<Datum>& args)>;
    using ValueEvaluator = std::function<Datum(const Datum& value)>;
    using ScriptResolver = std::function<Datum(const Datum& identifier, const std::optional<Datum>& scope)>;
    using ScriptHandlerFinder = std::function<std::optional<ScriptHandlerLocation>(
        int castLib,
        int memberNum,
        const std::string& handlerName)>;
    using ScriptChunkIdResolver = std::function<int(int castLib, int memberNum)>;
    using ScriptPropertyNamesResolver = std::function<std::vector<std::string>(int castLib, int memberNum)>;
    using AncestorCallHandler = std::function<Datum(const std::vector<Datum>& args)>;
    using RandomIntHandler = std::function<int(int max)>;
    using GetPrefHandler = std::function<Datum(const std::string& name)>;
    using SetPrefHandler = std::function<Datum(const std::string& name, const Datum& value)>;
    using OutputHandler = std::function<void(std::string_view kind, const std::string& text)>;
    using AlertHookHandler = std::function<bool(const std::string& alertType, const std::string& text)>;
    using AlertHandler = std::function<bool(const std::string& text)>;
    using ImagePaletteResolver = std::function<std::optional<ResolvedPalette>(const Datum& paletteRef)>;
    using ImportFileIntoHandler = std::function<bool(const Datum::CastMemberRef& ref,
                                                     const std::string& url,
                                                     const Datum& options)>;

    player::MovieProperties* movieProperties{nullptr};
    player::net::NetProvider* netManager{nullptr};
    player::audio::SoundManager* soundManager{nullptr};
    player::SpriteProperties* spriteProperties{nullptr};
    player::timeout::TimeoutManager* timeoutManager{nullptr};
    std::vector<Datum> currentHandlerArgs;
    const std::vector<Datum>* currentHandlerArgsView{nullptr};
    Datum returnValue{Datum::voidValue()};
    std::vector<std::pair<std::string, std::string>> externalParams;
    bool tellStreamStatusEnabled{false};
    bool debugPlaybackEnabled{false};
    bool returned{false};
    bool aborted{false};
    PuppetPaletteHandler puppetPaletteHandler;
    CastMemberCreator castMemberCreator;
    NamedCastMemberCreator namedCastMemberCreator;
    CastLibNumberResolver castLibNumberResolver;
    CastLibNameResolver castLibNameResolver;
    CastLibCountSupplier castLibCountSupplier;
    CastLibPropertyGetter castLibPropertyGetter;
    CastLibPropertySetter castLibPropertySetter;
    CastMemberCountSupplier castMemberCountSupplier;
    CastMemberResolver castMemberResolver;
    CastMemberNameResolver castMemberNameResolver;
    CastMemberExistsResolver castMemberExistsResolver;
    RegistryVisibleMemberResolver registryVisibleMemberResolver;
    CastMemberMethodHandler castMemberMethodHandler;
    CastMemberPropertyGetter castMemberPropertyGetter;
    CastMemberPropertySetter castMemberPropertySetter;
    SpriteMethodHandler spriteMethodHandler;
    FieldResolver fieldResolver;
    FieldParsedValueResolver fieldParsedValueResolver;
    FieldSetter fieldSetter;
    XtraRegisteredResolver xtraRegisteredResolver;
    XtraInstanceCreator xtraInstanceCreator;
    XtraHandler xtraHandler;
    XtraPropertyGetter xtraPropertyGetter;
    XtraPropertySetter xtraPropertySetter;
    CallTargetHandler callTargetHandler;
    ScriptInstanceMethodDeferrer scriptInstanceMethodDeferrer;
    NewInstanceHandler newInstanceHandler;
    ValueEvaluator valueEvaluator;
    ScriptResolver scriptResolver;
    ScriptHandlerFinder scriptHandlerFinder;
    ScriptChunkIdResolver scriptChunkIdResolver;
    ScriptPropertyNamesResolver scriptPropertyNamesResolver;
    AncestorCallHandler ancestorCallHandler;
    RandomIntHandler randomIntHandler;
    GetPrefHandler getPrefHandler;
    SetPrefHandler setPrefHandler;
    OutputHandler outputHandler;
    AlertHookHandler alertHookHandler;
    AlertHandler alertHandler;
    ImagePaletteResolver imagePaletteResolver;
    ImportFileIntoHandler importFileIntoHandler;
    std::unordered_map<std::string, Datum> scriptResolutionCache;
    mutable std::unordered_map<std::uint64_t,
                               std::unordered_map<std::string,
                                                  std::optional<ScriptHandlerLocation>,
                                                  TransparentCaseInsensitiveStringHash,
                                                  TransparentCaseInsensitiveStringEqual>>
        directScriptInstanceHandlerCache;
    mutable std::unordered_map<std::string, std::optional<ScriptHandlerLocation>> scriptInstanceHandlerCache;
    mutable std::unordered_map<std::string, vm::util::LineIndex> fieldLineIndexCache;
    mutable std::unordered_map<std::uint64_t, std::vector<std::string>> scriptPropertyNamesCache;
    mutable std::unordered_map<std::string, int> indexedCollectionLoopHeaderCache;
};

using BuiltinFunction = std::function<Datum(BuiltinContext& context, const std::vector<Datum>& args)>;
using BuiltinMap = std::unordered_map<std::string,
                                      BuiltinFunction,
                                      TransparentCaseInsensitiveStringHash,
                                      TransparentCaseInsensitiveStringEqual>;

class BuiltinRegistry {
public:
    BuiltinRegistry();

    [[nodiscard]] bool contains(std::string_view name) const;
    [[nodiscard]] Datum invoke(std::string_view name,
                               BuiltinContext& context,
                               const std::vector<Datum>& args = {}) const;
    [[nodiscard]] std::optional<Datum> invokeIfPresent(std::string_view name,
                                                       BuiltinContext& context,
                                                       const std::vector<Datum>& args = {}) const;
    [[nodiscard]] const BuiltinFunction* get(std::string_view name) const;

    void registerBuiltin(std::string_view name, BuiltinFunction function);
    [[nodiscard]] const BuiltinMap& map() const;

    [[nodiscard]] static std::string normalizeName(std::string_view name);

private:
    BuiltinMap builtins_;
};

class MathBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum abs(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum sqrt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum sin(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum cos(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum random(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum integer(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum toFloat(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum bitAnd(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum bitOr(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum bitXor(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum bitNot(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum power(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum min(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum max(BuiltinContext& context, const std::vector<Datum>& args);
};

class StringBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum string(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum length(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum chars(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum charToNum(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum numToChar(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum offset(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum stringReplace(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getPref(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum setPref(BuiltinContext& context, const std::vector<Datum>& args);
};

class OutputBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum put(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum alert(BuiltinContext& context, const std::vector<Datum>& args);
};

class ListBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum count(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getAt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum setAt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum addAt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum deleteAt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum append(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getaProp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum setaProp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum addProp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum deleteProp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getPropAt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum findPos(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getOne(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum deleteOne(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum sort(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum listp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum list(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum join(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum duplicate(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum convertToPropList(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getFirst(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getLast(BuiltinContext& context, const std::vector<Datum>& args);
};

class TimeoutBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum timeout(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum handleMethod(BuiltinContext& context,
                                            const Datum::TimeoutRef& ref,
                                            std::string_view methodName,
                                            const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::TimeoutRef& ref,
                                           std::string_view propName);
    static bool setProperty(BuiltinContext& context,
                            const Datum::TimeoutRef& ref,
                            std::string_view propName,
                            Datum value);
};

class NetBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum preloadNetThing(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum postNetText(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum netDone(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum netTextResult(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum netError(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getStreamStatus(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum tellStreamStatus(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum gotoNetPage(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum gotoNetMovie(BuiltinContext& context, const std::vector<Datum>& args);
};

class ExternalParamBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum externalParamValue(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum externalParamName(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum externalParamCount(BuiltinContext& context, const std::vector<Datum>& args);
};

class ImageBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum image(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum importFileInto(BuiltinContext& context, const std::vector<Datum>& args);
};

class SoundBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum beep(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum sound(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum soundEnabled(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum handleMethod(BuiltinContext& context,
                                            const Datum::SoundChannel& channel,
                                            std::string_view methodName,
                                            const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::SoundChannel& channel,
                                           std::string_view propName);
    static bool setProperty(BuiltinContext& context,
                            const Datum::SoundChannel& channel,
                            std::string_view propName,
                            Datum value);
};

class CastLibBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum castLib(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum member(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum field(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum createMember(BuiltinContext& context, const std::vector<Datum>& args);
};

class XtraBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum xtra(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum createInstance(BuiltinContext& context,
                                              const Datum::Xtra& xtraRef,
                                              const std::vector<Datum>& args);
    [[nodiscard]] static Datum callHandler(BuiltinContext& context,
                                           const Datum::XtraInstance& instance,
                                           std::string_view handlerName,
                                           const std::vector<Datum>& args);
    [[nodiscard]] static Datum callInstanceGlobalHandler(BuiltinContext& context,
                                                         std::string_view handlerName,
                                                         const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::XtraInstance& instance,
                                           std::string_view propertyName);
    static void setProperty(BuiltinContext& context,
                            const Datum::XtraInstance& instance,
                            std::string_view propertyName,
                            const Datum& value);
};

class ControlFlowBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum returnValue(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum halt(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum abort(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum nothing(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum param(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum go(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum call(BuiltinContext& context, const std::vector<Datum>& args);
};

class ConstructorBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum point(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum rect(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum unionRect(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum intersect(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum color(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum rgb(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum paletteIndex(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum sprite(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum newInstance(BuiltinContext& context, const std::vector<Datum>& args);
};

class TypeBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum objectp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum voidp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum value(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum script(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum ilk(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum listp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum stringp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum integerp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum floatp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum symbolp(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum symbol(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum callAncestor(BuiltinContext& context, const std::vector<Datum>& args);
};

class MovieBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum label(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum marker(BuiltinContext& context, const std::vector<Datum>& args);
};

class SpriteBuiltins {
public:
    static void registerBuiltins(BuiltinRegistry& registry);
    [[nodiscard]] static Datum puppetTempo(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum puppetSprite(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum puppetPalette(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum cursor(BuiltinContext& context, const std::vector<Datum>& args);
    [[nodiscard]] static Datum spriteBox(BuiltinContext& context, const std::vector<Datum>& args);
};

} // namespace libreshockwave::lingo::builtin
