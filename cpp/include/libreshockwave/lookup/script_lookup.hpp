#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "libreshockwave/chunks/cast_member_chunk.hpp"

namespace libreshockwave::chunks {
class ScriptChunk;
class ScriptContextChunk;
}

namespace libreshockwave::lookup {

class ScriptLookup {
public:
    ScriptLookup(std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts,
                 std::vector<std::shared_ptr<chunks::ScriptContextChunk>> script_contexts,
                 std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members);

    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getByContextId(int script_id) const;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> getAllByContextId(int script_id) const;
    [[nodiscard]] std::optional<chunks::CastMemberScriptType> getScriptType(const std::shared_ptr<chunks::ScriptChunk>& script) const;

private:
    std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::vector<std::shared_ptr<chunks::ScriptContextChunk>> script_contexts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members_;
};

} // namespace libreshockwave::lookup
