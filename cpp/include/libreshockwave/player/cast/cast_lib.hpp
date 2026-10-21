#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/moviesource/records/cast_asset.hpp"

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}

}

namespace libreshockwave::cast {
class CastMember;
}

namespace libreshockwave::chunks {
class CastChunk;
class CastMemberChunk;
class ScriptChunk;
class ScriptNamesChunk;
enum class ScriptChunkType;
}

namespace libreshockwave::player::cast {

class CastLib {
public:
    enum class State {
        None,
        Fetching,
        Loading,
        Loaded
    };

    CastLib(int number,
            std::shared_ptr<chunks::CastChunk> cast_chunk,
            const moviesource::CastListAsset::Entry* list_entry = nullptr);

    void setSourceFile(std::shared_ptr<moviesource::MovieSource> file);
    void setBitmapPaletteHostFile(std::shared_ptr<moviesource::MovieSource> file);
    [[nodiscard]] std::shared_ptr<moviesource::MovieSource> sourceFile() const;
    void reloadFromFile(std::shared_ptr<moviesource::MovieSource> file);

    [[nodiscard]] id::CastLibId castLibId() const;
    [[nodiscard]] int number() const;
    [[nodiscard]] const std::string& name() const;
    void setName(std::string name);
    [[nodiscard]] const std::string& fileName() const;
    void setFileName(std::string file_name);
    [[nodiscard]] bool isExternal() const;
    [[nodiscard]] bool isFetched() const;

    [[nodiscard]] bool hasAuthoredExternalBinding() const;
    [[nodiscard]] bool matchesAuthoredExternalFile(const std::string& base_name) const;
    [[nodiscard]] bool usesAuthoredExternalBinding() const;
    [[nodiscard]] bool usesStableRegistryBinding() const;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] bool isFetching() const;
    void markFetching();

    [[nodiscard]] int preloadMode() const;
    void setPreloadMode(int preload_mode);

    void load();
    [[nodiscard]] int memberCount();
    [[nodiscard]] const std::map<int, std::shared_ptr<chunks::CastMemberChunk>>& memberChunks();
    [[nodiscard]] const std::map<int, std::shared_ptr<libreshockwave::cast::CastMember>>& runtimeMembers();
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberByNumber(int member_number);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberByName(const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getMember(int member_number);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getMemberByName(const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getPaletteMemberByName(
        const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> createDynamicMember(
        const std::string& member_type);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> createDynamicMember(
        const std::string& member_name,
        const std::string& member_type);
    [[nodiscard]] bool hasMemberNumber(int member_number);
    [[nodiscard]] bool hasMemberNamedExact(const std::string& member_name);
    [[nodiscard]] int getMemberNumber(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScript(int member_number);
    [[nodiscard]] chunks::ScriptChunkType scriptTypeForScript(
        const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::ScriptChunk>>& allScripts();
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> scriptNames() const;

    [[nodiscard]] lingo::Datum getProp(const std::string& prop_name);
    bool setProp(const std::string& prop_name, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum getMemberProp(int member_number, const std::string& prop_name);
    bool setMemberProp(int member_number, const std::string& prop_name, const lingo::Datum& value);

    bool setExternalData(const std::vector<std::uint8_t>& data,
                         std::string_view source_name = {});
    void cacheFetchedExternalData(const std::vector<std::uint8_t>& data);

    [[nodiscard]] static lingo::Datum getInvalidMemberProp(const std::string& prop_name);
    static void registerFontAliases(const std::shared_ptr<moviesource::MovieSource>& file);

private:
    struct FontAliasInfo {
        std::string alias;
        std::string font_name;
        bool bold = false;
    };

    [[nodiscard]] int minMember() const;
    void loadMembersFromCast(const std::shared_ptr<chunks::CastChunk>& cast, int min_member);
    void loadFromExternalFile();
    void cacheScriptTypeForMember(int member_number, const std::shared_ptr<chunks::ScriptChunk>& script);
    void scanXmedFonts();
    void invalidateFileBackedBinding();
    [[nodiscard]] bool sameFileBinding(const std::string& current_file_name, const std::string& new_file_name) const;
    [[nodiscard]] bool usesGeneratedPlaceholderName(const std::string& candidate_name) const;
    [[nodiscard]] bool looksLikeDirectFileBindingName(const std::string& candidate_name) const;
    [[nodiscard]] std::string resolveMemberText(const std::shared_ptr<libreshockwave::cast::CastMember>& member);
    [[nodiscard]] int nextAvailableDynamicMemberNumber();
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberChunkByNameExact(const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> findMemberByNameExact(const std::string& member_name);
    void invalidateMemberNameIndex() const;
    void rebuildMemberNameIndex() const;

    [[nodiscard]] static std::optional<std::string> sourcePrefixedLookupName(const std::string& requested_name);
    [[nodiscard]] static libreshockwave::cast::MemberType dynamicMemberTypeFor(const std::string& type_name);
    [[nodiscard]] static bool sameAuthoredMember(const std::shared_ptr<chunks::CastMemberChunk>& left,
                                                 const std::shared_ptr<chunks::CastMemberChunk>& right);
    [[nodiscard]] static std::optional<FontAliasInfo> parseFontAlias(const std::vector<std::uint8_t>& data,
                                                                     const std::string& member_name);
    [[nodiscard]] static std::vector<std::string> extractPrintableNullTerminatedStrings(
        const std::vector<std::uint8_t>& data);

    id::CastLibId cast_lib_id_;
    std::string name_;
    std::string file_name_;
    std::string authored_file_name_;
    bool name_explicitly_assigned_ = false;
    bool name_loaded_from_external_file_ = false;
    State state_ = State::None;
    int preload_mode_ = 0;
    lingo::Datum selection_;

    std::shared_ptr<chunks::CastChunk> cast_chunk_;
    std::shared_ptr<moviesource::MovieSource> source_file_;
    std::shared_ptr<moviesource::MovieSource> bitmap_palette_host_file_;
    std::vector<std::uint8_t> fetched_external_data_;
    // File name of the fetched external data, used to pick the format parser.
    std::string fetched_external_name_;
    std::map<int, std::shared_ptr<chunks::CastMemberChunk>> member_chunks_;
    std::map<int, std::shared_ptr<libreshockwave::cast::CastMember>> members_;
    std::map<int, std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::unordered_map<const chunks::ScriptChunk*, chunks::ScriptChunkType> script_types_by_pointer_;
    std::unordered_map<int, chunks::ScriptChunkType> script_types_by_id_;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> cached_scripts_;
    mutable bool member_name_index_dirty_ = true;
    mutable std::unordered_map<std::string, int> member_chunk_name_index_;
    mutable std::unordered_map<std::string, int> runtime_member_name_index_;
    int total_slot_count_ = 0;
    int next_dynamic_member_ = 10000;
};

} // namespace libreshockwave::player::cast
