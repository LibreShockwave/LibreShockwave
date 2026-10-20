#include "libreshockwave/lookup/script_lookup.hpp"

#include <utility>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_context_chunk.hpp"

namespace libreshockwave::lookup {

ScriptLookup::ScriptLookup(std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts,
                           std::vector<std::shared_ptr<chunks::ScriptContextChunk>> script_contexts,
                           std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members)
    : scripts_(std::move(scripts)),
      script_contexts_(std::move(script_contexts)),
      cast_members_(std::move(cast_members)) {}

std::shared_ptr<chunks::ScriptChunk> ScriptLookup::getByContextId(int script_id) const {
    const auto matches = getAllByContextId(script_id);
    if (!matches.empty()) {
        return matches.front();
    }
    return nullptr;
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> ScriptLookup::getAllByContextId(int script_id) const {
    std::vector<std::shared_ptr<chunks::ScriptChunk>> matches;
    const int index = script_id - 1;

    for (const auto& context : script_contexts_) {
        if (!context || index < 0 || index >= static_cast<int>(context->entries().size())) {
            continue;
        }
        const auto& entry = context->entries()[static_cast<std::size_t>(index)];
        if (entry.id.value() <= 0) {
            continue;
        }
        for (const auto& script : scripts_) {
            if (script && script->id().value() == entry.id.value()) {
                matches.push_back(script);
                break;
            }
        }
    }

    for (const auto& script : scripts_) {
        if (script && script->id().value() == script_id) {
            matches.push_back(script);
            break;
        }
    }

    return matches;
}

std::optional<chunks::CastMemberScriptType> ScriptLookup::getScriptType(const std::shared_ptr<chunks::ScriptChunk>& script) const {
    if (!script) {
        return std::nullopt;
    }

    for (const auto& context : script_contexts_) {
        if (!context) {
            continue;
        }
        for (int index = 0; index < static_cast<int>(context->entries().size()); ++index) {
            if (context->entries()[static_cast<std::size_t>(index)].id.value() != script->id().value()) {
                continue;
            }
            const int script_id = index + 1;
            for (const auto& member : cast_members_) {
                if (member && member->isScript() && member->scriptId() == script_id) {
                    return member->getScriptType();
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace libreshockwave::lookup
