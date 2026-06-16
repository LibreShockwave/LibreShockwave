#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave {
class DirectorFile;
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
            std::shared_ptr<chunks::CastChunk> castChunk,
            const chunks::CastListChunk::CastListEntry* listEntry = nullptr);

    void setSourceFile(std::shared_ptr<DirectorFile> file);
    [[nodiscard]] std::shared_ptr<DirectorFile> sourceFile() const;
    void reloadFromFile(std::shared_ptr<DirectorFile> file);

    [[nodiscard]] id::CastLibId castLibId() const;
    [[nodiscard]] int number() const;
    [[nodiscard]] const std::string& name() const;
    void setName(std::string name);
    [[nodiscard]] const std::string& fileName() const;
    void setFileName(std::string fileName);
    [[nodiscard]] bool isExternal() const;
    [[nodiscard]] bool isFetched() const;

    [[nodiscard]] bool hasAuthoredExternalBinding() const;
    [[nodiscard]] bool matchesAuthoredExternalFile(const std::string& baseName) const;
    [[nodiscard]] bool usesAuthoredExternalBinding() const;
    [[nodiscard]] bool usesStableRegistryBinding() const;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] bool isFetching() const;
    void markFetching();

    [[nodiscard]] int preloadMode() const;
    void setPreloadMode(int preloadMode);

    void load();
    [[nodiscard]] int memberCount();
    [[nodiscard]] const std::map<int, std::shared_ptr<chunks::CastMemberChunk>>& memberChunks();
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberByNumber(int memberNumber);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberByName(const std::string& memberName);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getMember(int memberNumber);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getMemberByName(const std::string& memberName);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> getPaletteMemberByName(
        const std::string& memberName);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> createDynamicMember(
        const std::string& memberType);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> createDynamicMember(
        const std::string& memberName,
        const std::string& memberType);
    [[nodiscard]] bool hasMemberNumber(int memberNumber);
    [[nodiscard]] bool hasMemberNamedExact(const std::string& memberName);
    [[nodiscard]] int getMemberNumber(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScript(int memberNumber);
    [[nodiscard]] chunks::ScriptChunkType scriptTypeForScript(
        const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::ScriptChunk>>& allScripts();
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> scriptNames() const;

    [[nodiscard]] lingo::Datum getProp(const std::string& propName);
    bool setProp(const std::string& propName, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum getMemberProp(int memberNumber, const std::string& propName);
    bool setMemberProp(int memberNumber, const std::string& propName, const lingo::Datum& value);

    bool setExternalData(const std::vector<std::uint8_t>& data);
    void cacheFetchedExternalData(const std::vector<std::uint8_t>& data);

    [[nodiscard]] static lingo::Datum getInvalidMemberProp(const std::string& propName);
    static void registerFontAliases(const std::shared_ptr<DirectorFile>& file);

private:
    struct FontAliasInfo {
        std::string alias;
        std::string fontName;
        bool bold = false;
    };

    [[nodiscard]] int minMember() const;
    void loadMembersFromCast(const std::shared_ptr<chunks::CastChunk>& cast, int minMember);
    void loadFromExternalFile();
    void cacheScriptTypeForMember(int memberNumber, const std::shared_ptr<chunks::ScriptChunk>& script);
    void scanXmedFonts();
    void invalidateFileBackedBinding();
    [[nodiscard]] bool sameFileBinding(const std::string& currentFileName, const std::string& newFileName) const;
    [[nodiscard]] bool usesGeneratedPlaceholderName(const std::string& candidateName) const;
    [[nodiscard]] bool looksLikeDirectFileBindingName(const std::string& candidateName) const;
    [[nodiscard]] std::string resolveMemberText(const std::shared_ptr<libreshockwave::cast::CastMember>& member);
    [[nodiscard]] int nextAvailableDynamicMemberNumber();
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> findMemberChunkByNameExact(const std::string& memberName);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> findMemberByNameExact(const std::string& memberName);
    void invalidateMemberNameIndex() const;
    void rebuildMemberNameIndex() const;

    [[nodiscard]] static std::optional<std::string> sourcePrefixedLookupName(const std::string& requestedName);
    [[nodiscard]] static libreshockwave::cast::MemberType dynamicMemberTypeFor(const std::string& typeName);
    [[nodiscard]] static bool sameAuthoredMember(const std::shared_ptr<chunks::CastMemberChunk>& left,
                                                 const std::shared_ptr<chunks::CastMemberChunk>& right);
    [[nodiscard]] static std::optional<FontAliasInfo> parseFontAlias(const std::vector<std::uint8_t>& data,
                                                                     const std::string& memberName);
    [[nodiscard]] static std::vector<std::string> extractPrintableNullTerminatedStrings(
        const std::vector<std::uint8_t>& data);

    id::CastLibId castLibId_;
    std::string name_;
    std::string fileName_;
    std::string authoredFileName_;
    bool nameExplicitlyAssigned_ = false;
    bool nameLoadedFromExternalFile_ = false;
    State state_ = State::None;
    int preloadMode_ = 0;
    lingo::Datum selection_;

    std::shared_ptr<chunks::CastChunk> castChunk_;
    std::shared_ptr<DirectorFile> sourceFile_;
    std::vector<std::uint8_t> fetchedExternalData_;
    std::map<int, std::shared_ptr<chunks::CastMemberChunk>> memberChunks_;
    std::map<int, std::shared_ptr<libreshockwave::cast::CastMember>> members_;
    std::map<int, std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::unordered_map<const chunks::ScriptChunk*, chunks::ScriptChunkType> scriptTypesByPointer_;
    std::unordered_map<int, chunks::ScriptChunkType> scriptTypesById_;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> cachedScripts_;
    mutable bool memberNameIndexDirty_ = true;
    mutable std::unordered_map<std::string, int> memberChunkNameIndex_;
    mutable std::unordered_map<std::string, int> runtimeMemberNameIndex_;
    int totalSlotCount_ = 0;
    int nextDynamicMember_ = 10000;
};

} // namespace libreshockwave::player::cast
