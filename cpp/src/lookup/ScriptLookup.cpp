#include "libreshockwave/lookup/ScriptLookup.hpp"

#include <utility>

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/ScriptContextChunk.hpp"

namespace libreshockwave::lookup {

ScriptLookup::ScriptLookup(std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts,
                           std::vector<std::shared_ptr<chunks::ScriptContextChunk>> scriptContexts,
                           std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers)
    : scripts_(std::move(scripts)),
      scriptContexts_(std::move(scriptContexts)),
      castMembers_(std::move(castMembers)) {}

std::shared_ptr<chunks::ScriptChunk> ScriptLookup::getByContextId(int scriptId) const {
    const auto matches = getAllByContextId(scriptId);
    if (!matches.empty()) {
        return matches.front();
    }
    return nullptr;
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> ScriptLookup::getAllByContextId(int scriptId) const {
    std::vector<std::shared_ptr<chunks::ScriptChunk>> matches;
    const int index = scriptId - 1;

    for (const auto& context : scriptContexts_) {
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
        if (script && script->id().value() == scriptId) {
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

    for (const auto& context : scriptContexts_) {
        if (!context) {
            continue;
        }
        for (int index = 0; index < static_cast<int>(context->entries().size()); ++index) {
            if (context->entries()[static_cast<std::size_t>(index)].id.value() != script->id().value()) {
                continue;
            }
            const int scriptId = index + 1;
            for (const auto& member : castMembers_) {
                if (member && member->isScript() && member->scriptId() == scriptId) {
                    return member->getScriptType();
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace libreshockwave::lookup
