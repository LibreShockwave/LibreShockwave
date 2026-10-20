#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

namespace libreshockwave::chunks {
class CastChunk;
class CastListChunk;
class CastMemberChunk;
class ConfigChunk;
}

namespace libreshockwave::lookup {

class CastMemberLookup {
public:
    CastMemberLookup(std::vector<std::shared_ptr<chunks::CastChunk>> casts,
                     std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members,
                     std::shared_ptr<chunks::CastListChunk> cast_list,
                     std::shared_ptr<chunks::ConfigChunk> config);

    [[nodiscard]] std::shared_ptr<chunks::CastChunk> getMappedCast(int cast_lib) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getByIndex(int cast_lib, int cast_member_index) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getByNumber(int cast_lib, int member_number) const;

private:
    [[nodiscard]] std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> buildCastLibMapping() const;
    [[nodiscard]] int getMinMember(int cast_lib) const;

    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members_;
    std::shared_ptr<chunks::CastListChunk> cast_list_;
    std::shared_ptr<chunks::ConfigChunk> config_;
    std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> cast_lib_to_ca_sp_;
};

} // namespace libreshockwave::lookup
