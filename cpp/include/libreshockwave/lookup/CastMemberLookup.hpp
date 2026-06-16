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
                     std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers,
                     std::shared_ptr<chunks::CastListChunk> castList,
                     std::shared_ptr<chunks::ConfigChunk> config);

    [[nodiscard]] std::shared_ptr<chunks::CastChunk> getMappedCast(int castLib) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getByIndex(int castLib, int castMemberIndex) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getByNumber(int castLib, int memberNumber) const;

private:
    [[nodiscard]] std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> buildCastLibMapping() const;
    [[nodiscard]] int getMinMember(int castLib) const;

    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers_;
    std::shared_ptr<chunks::CastListChunk> castList_;
    std::shared_ptr<chunks::ConfigChunk> config_;
    std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> castLibToCASp_;
};

} // namespace libreshockwave::lookup
