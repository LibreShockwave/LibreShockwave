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

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/vm/util/string_chunk_utils.hpp"
#include "libreshockwave/player/movie_properties.hpp"
#include "libreshockwave/player/audio/sound_manager.hpp"
#include "libreshockwave/player/net/net_manager.hpp"
#include "libreshockwave/player/sprite_properties.hpp"
#include "libreshockwave/player/timeout/timeout_manager.hpp"

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
        std::optional<Datum::CastMemberRef> member_ref;
        std::optional<std::string> system_name;
    };

    struct ScriptHandlerLocation {
        ScriptHandlerLocation() = default;
        ScriptHandlerLocation(const chunks::ScriptChunk* script_value,
                              const chunks::ScriptChunk::Handler* handler_value,
                              std::shared_ptr<const chunks::ScriptChunk> script_owner_value = nullptr,
                              std::shared_ptr<const DirectorFile> file_owner_value = nullptr,
                              std::shared_ptr<const chunks::ScriptNamesChunk> script_names_owner_value = nullptr,
                              chunks::ScriptChunkType script_type_value = chunks::ScriptChunkType::Unknown)
            : script(script_value),
              handler(handler_value),
              script_owner(std::move(script_owner_value)),
              file_owner(std::move(file_owner_value)),
              script_names_owner(std::move(script_names_owner_value)),
              script_type(script_type_value) {}
        ScriptHandlerLocation(const chunks::ScriptChunk* script_value,
                              const chunks::ScriptChunk::Handler& handler_value,
                              std::shared_ptr<const chunks::ScriptChunk> script_owner_value = nullptr,
                              std::shared_ptr<const DirectorFile> file_owner_value = nullptr,
                              std::shared_ptr<const chunks::ScriptNamesChunk> script_names_owner_value = nullptr,
                              chunks::ScriptChunkType script_type_value = chunks::ScriptChunkType::Unknown)
            : ScriptHandlerLocation(script_value,
                                    &handler_value,
                                    std::move(script_owner_value),
                                    std::move(file_owner_value),
                                    std::move(script_names_owner_value),
                                    script_type_value) {}

        const chunks::ScriptChunk* script{nullptr};
        const chunks::ScriptChunk::Handler* handler{nullptr};
        std::shared_ptr<const chunks::ScriptChunk> script_owner;
        std::shared_ptr<const DirectorFile> file_owner;
        std::shared_ptr<const chunks::ScriptNamesChunk> script_names_owner;
        chunks::ScriptChunkType script_type{chunks::ScriptChunkType::Unknown};
    };

    using PuppetPaletteHandler = std::function<void(std::optional<Datum> palette_ref)>;
    using CastMemberCreator = std::function<Datum(int cast_lib, const std::string& member_type)>;
    using NamedCastMemberCreator = std::function<Datum(const std::string& member_name, const std::string& member_type)>;
    using CastLibNumberResolver = std::function<int(int cast_lib)>;
    using CastLibNameResolver = std::function<int(const std::string& name)>;
    using CastLibCountSupplier = std::function<int()>;
    using CastLibPropertyGetter = std::function<Datum(int cast_lib, const std::string& property_name)>;
    using CastLibPropertySetter = std::function<bool(int cast_lib, const std::string& property_name, const Datum& value)>;
    using CastMemberCountSupplier = std::function<int(int cast_lib)>;
    using CastMemberResolver = std::function<Datum(int cast_lib, int member_num)>;
    using CastMemberNameResolver = std::function<Datum(int cast_lib, const std::string& member_name)>;
    using CastMemberExistsResolver = std::function<bool(int cast_lib, int member_num)>;
    using RegistryVisibleMemberResolver = std::function<bool(int cast_lib, int member_num)>;
    using CastMemberMethodHandler = std::function<Datum(int cast_lib,
                                                        int member_num,
                                                        const std::string& method_name,
                                                        const std::vector<Datum>& args)>;
    using CastMemberPropertyGetter = std::function<Datum(int cast_lib, int member_num, const std::string& property_name)>;
    using CastMemberPropertySetter = std::function<bool(int cast_lib,
                                                        int member_num,
                                                        const std::string& property_name,
                                                        const Datum& value)>;
    using SpriteMethodHandler = std::function<Datum(int channel,
                                                    const std::string& method_name,
                                                    const std::vector<Datum>& args)>;
    using FieldResolver = std::function<Datum(const Datum& identifier, int cast_lib)>;
    using FieldParsedValueResolver = std::function<Datum(int cast_lib, int member_num, std::uint64_t revision)>;
    using FieldSetter = std::function<void(const Datum& identifier, int cast_lib, const std::string& value)>;
    using XtraRegisteredResolver = std::function<bool(const std::string& xtra_name)>;
    using XtraInstanceCreator = std::function<Datum(const std::string& xtra_name, const std::vector<Datum>& args)>;
    using XtraHandler = std::function<Datum(const Datum::XtraInstance& instance,
                                            const std::string& handler_name,
                                            const std::vector<Datum>& args)>;
    using XtraPropertyGetter = std::function<Datum(const Datum::XtraInstance& instance, const std::string& property_name)>;
    using XtraPropertySetter = std::function<void(const Datum::XtraInstance& instance,
                                                  const std::string& property_name,
                                                  const Datum& value)>;
    using CallTargetHandler = std::function<Datum(const Datum& target,
                                                  const std::string& handler_name,
                                                  const std::vector<Datum>& args)>;
    using SendAllSpritesHandler = std::function<Datum(const std::string& handler_name,
                                                      const std::vector<Datum>& args)>;
    using ScriptInstanceMethodDeferrer = std::function<bool(const Datum& instance,
                                                            const std::string& method_name,
                                                            const std::vector<Datum>& args)>;
    using NewInstanceHandler = std::function<Datum(const Datum& target, const std::vector<Datum>& args)>;
    using ValueEvaluator = std::function<Datum(const Datum& value)>;
    using ScriptResolver = std::function<Datum(const Datum& identifier, const std::optional<Datum>& scope)>;
    using ScriptHandlerFinder = std::function<std::optional<ScriptHandlerLocation>(
        int cast_lib,
        int member_num,
        const std::string& handler_name)>;
    using ScriptChunkIdResolver = std::function<int(int cast_lib, int member_num)>;
    using ScriptPropertyNamesResolver = std::function<std::vector<std::string>(int cast_lib, int member_num)>;
    using AncestorCallHandler = std::function<Datum(const std::vector<Datum>& args)>;
    using RandomIntHandler = std::function<int(int max)>;
    using GetPrefHandler = std::function<Datum(const std::string& name)>;
    using SetPrefHandler = std::function<Datum(const std::string& name, const Datum& value)>;
    using OutputHandler = std::function<void(std::string_view kind, const std::string& text)>;
    using AlertHookHandler = std::function<bool(const std::string& alert_type, const std::string& text)>;
    using AlertHandler = std::function<bool(const std::string& text)>;
    using ImagePaletteResolver = std::function<std::optional<ResolvedPalette>(const Datum& palette_ref)>;
    using ImportFileIntoHandler = std::function<bool(const Datum::CastMemberRef& ref,
                                                     const std::string& url,
                                                     const Datum& options)>;

    player::MovieProperties* movie_properties{nullptr};
    player::net::NetProvider* net_manager{nullptr};
    player::audio::SoundManager* sound_manager{nullptr};
    player::SpriteProperties* sprite_properties{nullptr};
    player::timeout::TimeoutManager* timeout_manager{nullptr};
    std::vector<Datum> current_handler_args;
    const std::vector<Datum>* current_handler_args_view{nullptr};
    Datum return_value{Datum::voidValue()};
    std::vector<std::pair<std::string, std::string>> external_params;
    bool tell_stream_status_enabled{false};
    bool debug_playback_enabled{false};
    bool returned{false};
    bool aborted{false};
    PuppetPaletteHandler puppet_palette_handler;
    CastMemberCreator cast_member_creator;
    NamedCastMemberCreator named_cast_member_creator;
    CastLibNumberResolver cast_lib_number_resolver;
    CastLibNameResolver cast_lib_name_resolver;
    CastLibCountSupplier cast_lib_count_supplier;
    CastLibPropertyGetter cast_lib_property_getter;
    CastLibPropertySetter cast_lib_property_setter;
    CastMemberCountSupplier cast_member_count_supplier;
    CastMemberResolver cast_member_resolver;
    CastMemberNameResolver cast_member_name_resolver;
    CastMemberExistsResolver cast_member_exists_resolver;
    RegistryVisibleMemberResolver registry_visible_member_resolver;
    CastMemberMethodHandler cast_member_method_handler;
    CastMemberPropertyGetter cast_member_property_getter;
    CastMemberPropertySetter cast_member_property_setter;
    SpriteMethodHandler sprite_method_handler;
    FieldResolver field_resolver;
    FieldParsedValueResolver field_parsed_value_resolver;
    FieldSetter field_setter;
    XtraRegisteredResolver xtra_registered_resolver;
    XtraInstanceCreator xtra_instance_creator;
    XtraHandler xtra_handler;
    XtraPropertyGetter xtra_property_getter;
    XtraPropertySetter xtra_property_setter;
    CallTargetHandler call_target_handler;
    SendAllSpritesHandler send_all_sprites_handler;
    ScriptInstanceMethodDeferrer script_instance_method_deferrer;
    NewInstanceHandler new_instance_handler;
    ValueEvaluator value_evaluator;
    ScriptResolver script_resolver;
    ScriptHandlerFinder script_handler_finder;
    ScriptChunkIdResolver script_chunk_id_resolver;
    ScriptPropertyNamesResolver script_property_names_resolver;
    AncestorCallHandler ancestor_call_handler;
    RandomIntHandler random_int_handler;
    GetPrefHandler get_pref_handler;
    SetPrefHandler set_pref_handler;
    OutputHandler output_handler;
    AlertHookHandler alert_hook_handler;
    AlertHandler alert_handler;
    ImagePaletteResolver image_palette_resolver;
    ImportFileIntoHandler import_file_into_handler;
    std::unordered_map<std::string, Datum> script_resolution_cache;
    mutable std::unordered_map<std::uint64_t,
                               std::unordered_map<std::string,
                                                  std::optional<ScriptHandlerLocation>,
                                                  TransparentCaseInsensitiveStringHash,
                                                  TransparentCaseInsensitiveStringEqual>>
        direct_script_instance_handler_cache;
    mutable std::unordered_map<std::string, std::optional<ScriptHandlerLocation>> script_instance_handler_cache;
    mutable std::unordered_map<std::string, vm::util::LineIndex> field_line_index_cache;
    mutable std::unordered_map<std::uint64_t, std::vector<std::string>> script_property_names_cache;
    mutable std::unordered_map<std::string, int> indexed_collection_loop_header_cache;
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
                                            std::string_view method_name,
                                            const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::TimeoutRef& ref,
                                           std::string_view prop_name);
    static bool setProperty(BuiltinContext& context,
                            const Datum::TimeoutRef& ref,
                            std::string_view prop_name,
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
                                            std::string_view method_name,
                                            const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::SoundChannel& channel,
                                           std::string_view prop_name);
    static bool setProperty(BuiltinContext& context,
                            const Datum::SoundChannel& channel,
                            std::string_view prop_name,
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
                                              const Datum::Xtra& xtra_ref,
                                              const std::vector<Datum>& args);
    [[nodiscard]] static Datum callHandler(BuiltinContext& context,
                                           const Datum::XtraInstance& instance,
                                           std::string_view handler_name,
                                           const std::vector<Datum>& args);
    [[nodiscard]] static Datum callInstanceGlobalHandler(BuiltinContext& context,
                                                         std::string_view handler_name,
                                                         const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(BuiltinContext& context,
                                           const Datum::XtraInstance& instance,
                                           std::string_view property_name);
    static void setProperty(BuiltinContext& context,
                            const Datum::XtraInstance& instance,
                            std::string_view property_name,
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
    [[nodiscard]] static Datum sendAllSprites(BuiltinContext& context, const std::vector<Datum>& args);
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
