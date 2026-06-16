#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "libreshockwave/chunks/CastMemberChunk.hpp"

namespace libreshockwave::chunks {
class ScriptChunk;
class ScriptContextChunk;
}

namespace libreshockwave::lookup {

class ScriptLookup {
public:
    ScriptLookup(std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts,
                 std::vector<std::shared_ptr<chunks::ScriptContextChunk>> scriptContexts,
                 std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers);

    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getByContextId(int scriptId) const;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> getAllByContextId(int scriptId) const;
    [[nodiscard]] std::optional<chunks::CastMemberScriptType> getScriptType(const std::shared_ptr<chunks::ScriptChunk>& script) const;

private:
    std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::vector<std::shared_ptr<chunks::ScriptContextChunk>> scriptContexts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers_;
};

} // namespace libreshockwave::lookup
