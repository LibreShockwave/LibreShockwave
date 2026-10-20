#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/builtin/builtin_registry.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::cast {
class CastMember;
}

namespace libreshockwave::bitmap {
class Palette;
}

namespace libreshockwave::chunks {
class CastMemberChunk;
}

namespace libreshockwave::player::render::output {
class TextRenderer;
}

namespace libreshockwave::player::cast {

class CastLibManager {
public:
    using CastDataRequestCallback = std::function<void(int cast_lib_number, const std::string& file_name)>;
    using MemberSlotRetiredCallback = std::function<void(int cast_lib_number, int member_number)>;

    explicit CastLibManager(std::shared_ptr<DirectorFile> file,
                            CastDataRequestCallback cast_data_request_callback = {});

    [[nodiscard]] std::shared_ptr<DirectorFile> file() const;

    [[nodiscard]] std::shared_ptr<CastLib> getCastLib(int cast_lib_number);
    [[nodiscard]] std::shared_ptr<CastLib> getCastLibByNameInternal(const std::string& name);
    [[nodiscard]] int getCastLibByNumber(int cast_lib_number);
    [[nodiscard]] int getCastLibByName(const std::string& name);
    [[nodiscard]] int getCastLibCount();
    [[nodiscard]] std::map<int, std::shared_ptr<CastLib>>& castLibs();
    [[nodiscard]] const std::map<int, std::shared_ptr<CastLib>>& castLibs() const;

    [[nodiscard]] lingo::Datum getCastLibProp(int cast_lib_number, const std::string& prop_name);
    bool setCastLibProp(int cast_lib_number, const std::string& prop_name, const lingo::Datum& value);
    [[nodiscard]] std::string getCastLibName(int cast_lib_number);
    [[nodiscard]] std::string getCastLibFileName(int cast_lib_number);
    [[nodiscard]] bool fetchCastLib(int cast_lib_number);
    [[nodiscard]] bool isCastLibExternal(int cast_lib_number);
    void preloadCasts(int mode);

    [[nodiscard]] lingo::Datum getMember(int cast_lib_number, int member_number);
    [[nodiscard]] lingo::Datum getMemberByName(int cast_lib_number, const std::string& member_name);
    [[nodiscard]] bool memberExists(int cast_lib_number, int member_number);
    [[nodiscard]] bool isRegistryVisibleMember(int cast_lib_number, int member_number);
    [[nodiscard]] int getMemberCount(int cast_lib_number);
    [[nodiscard]] lingo::Datum getMemberProp(int cast_lib_number, int member_number, const std::string& prop_name);
    bool setMemberProp(int cast_lib_number, int member_number, const std::string& prop_name, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum getFieldValue(const lingo::Datum& identifier, int cast_lib_number);
    [[nodiscard]] lingo::Datum getFieldDatum(const lingo::Datum& identifier, int cast_lib_number);
    [[nodiscard]] lingo::Datum getParsedFieldValue(int cast_lib_number, int member_number, std::uint64_t revision = 0);
    [[nodiscard]] std::vector<int> charPosToLoc(int cast_lib_number, int member_number, int char_index, int field_width);
    [[nodiscard]] int locToCharPos(int cast_lib_number, int member_number, int x, int y, int field_width);
    [[nodiscard]] int textLineHeight(int cast_lib_number, int member_number);
    void setFieldValue(const lingo::Datum& identifier, int cast_lib_number, const std::string& value);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMember(int cast_lib_number, int member_number);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMemberByName(const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> resolveMember(int cast_lib_number, int member_number);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> findCastMemberByName(const std::string& member_name);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> findRuntimeMember(
        const std::shared_ptr<chunks::CastMemberChunk>& target);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMember(int cast_lib_number, int member_number);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteById(int cast_lib_number, int palette_id);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByName(const std::string& name);
    [[nodiscard]] int getScriptChunkId(int cast_lib_number, int member_number);
    [[nodiscard]] std::vector<std::string> getScriptPropertyNames(int cast_lib_number, int member_number);
    [[nodiscard]] lingo::Datum createMember(int cast_lib_number, const std::string& member_type);
    [[nodiscard]] lingo::Datum createMember(const std::string& member_name, const std::string& member_type);
    bool eraseMember(int cast_lib_number, int member_number);
    bool exchangeMemberMedia(int first_cast_lib_number,
                             int first_member_number,
                             int second_cast_lib_number,
                             int second_member_number);
    [[nodiscard]] lingo::Datum callMemberMethod(int cast_lib_number,
                                                int member_number,
                                                const std::string& method_name,
                                                const std::vector<lingo::Datum>& args);
    bool importFileIntoMember(int cast_lib_number,
                              int member_number,
                              const std::string& url,
                              const lingo::Datum& options);

    void cacheExternalData(const std::string& url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getCachedExternalData(const std::string& base_name) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getCachedDownloadedData(const std::string& url) const;
    bool setExternalCastData(int cast_lib_number, const std::vector<std::uint8_t>& data);
    bool setExternalCastDataByUrl(const std::string& url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] std::vector<int> getMatchingCastLibNumbersByUrl(const std::string& url);
    [[nodiscard]] std::vector<int> getRequestedExternalCastSlots(const std::string& url);
    void clearPendingExternalLoad(int cast_lib_number);
    void setMemberSlotRetiredCallback(MemberSlotRetiredCallback callback);
    void setTextRenderer(render::output::TextRenderer* renderer);

    void installBuiltinCallbacks(lingo::builtin::BuiltinContext& context);

private:
    void ensureInitialized();
    void tryLoadCastFromCache(int cast_lib_number, const std::string& new_file_name);
    void markPendingExternalLoad(int cast_lib_number, const std::string& file_name);
    [[nodiscard]] std::vector<std::shared_ptr<CastLib>> findCastLibsByUrl(const std::string& url);
    [[nodiscard]] static std::vector<std::string> downloadCacheKeys(const std::string& url);
    bool copyMemberMedia(int target_cast_lib_number,
                         int target_member_number,
                         const lingo::Datum::CastMemberRef& source_ref);
    [[nodiscard]] static lingo::Datum getMemberByNameInCast(const std::shared_ptr<CastLib>& cast_lib,
                                                            const std::string& member_name);
    [[nodiscard]] static bool isRegistryFallbackEligibleCast(const std::shared_ptr<CastLib>& cast_lib);
    [[nodiscard]] std::shared_ptr<libreshockwave::cast::CastMember> resolveFieldMember(
        const lingo::Datum& identifier,
        int cast_lib_number);

    std::shared_ptr<DirectorFile> file_;
    std::map<int, std::shared_ptr<CastLib>> cast_libs_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> cast_data_cache_;
    std::map<int, std::string> pending_external_loads_;
    CastDataRequestCallback cast_data_request_callback_;
    MemberSlotRetiredCallback member_slot_retired_callback_;
    render::output::TextRenderer* text_renderer_{nullptr};
    struct ParsedFieldCacheEntry {
        std::uint64_t revision{0};
        std::string text;
        lingo::Datum value{lingo::Datum::voidValue()};
    };

    std::map<std::pair<int, int>, ParsedFieldCacheEntry> parsed_field_cache_;
    bool initialized_ = false;
};

} // namespace libreshockwave::player::cast
