#include "libreshockwave/moviesource/records/script_asset.hpp"

#include <sstream>

#include "libreshockwave/lingo/opcode.hpp"
#include "libreshockwave/format/script_format_utils.hpp"
#include "libreshockwave/util/string_utils.hpp"

namespace libreshockwave::moviesource {

ScriptType scriptTypeFromCode(int code) {
    switch (code) {
        case 1:
            return ScriptType::Score;
        case 2:
            return ScriptType::Behavior;
        case 3:
            return ScriptType::MovieScript;
        case 7:
            return ScriptType::Parent;
        default:
            return ScriptType::Unknown;
    }
}

std::string ScriptAsset::Instruction::toString() const {
    std::ostringstream out;
    out << "[" << offset << "] " << lingo::mnemonic(opcode);
    if (raw_opcode >= 0x40) {
        out << " " << argument;
    }
    return out.str();
}

int ScriptAsset::Handler::getInstructionIndex(int offset) const {
    if (const auto found = bytecode_index_map.find(offset); found != bytecode_index_map.end()) {
        return found->second;
    }
    return -1;
}

std::string ScriptAsset::resolveName(int name_id) const {
    if (name_id >= 0 && name_id < static_cast<int>(names.size())) {
        return names[static_cast<std::size_t>(name_id)];
    }
    return "#" + std::to_string(name_id);
}

std::string ScriptAsset::getHandlerName(const Handler& handler) const {
    if (handler.name_id >= 0 && handler.name_id < static_cast<int>(names.size())) {
        return names[static_cast<std::size_t>(handler.name_id)];
    }
    return "handler#" + std::to_string(handler.name_id);
}

const ScriptAsset::Handler* ScriptAsset::findHandlerByNameIdPtr(int name_id) const {
    for (const auto& handler : handlers) {
        if (handler.name_id == name_id) {
            return &handler;
        }
    }
    return nullptr;
}

std::optional<ScriptAsset::Handler> ScriptAsset::findHandlerByNameId(int name_id) const {
    const auto* handler = findHandlerByNameIdPtr(name_id);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

const ScriptAsset::Handler* ScriptAsset::findHandlerPtr(std::string_view name) const {
    for (const auto& handler : handlers) {
        if (util::equalsIgnoreCase(getHandlerName(handler), name)) {
            return &handler;
        }
    }
    return nullptr;
}

std::optional<ScriptAsset::Handler> ScriptAsset::findHandler(std::string_view name) const {
    const auto* handler = findHandlerPtr(name);
    if (handler != nullptr) {
        return *handler;
    }
    return std::nullopt;
}

std::vector<std::string> ScriptAsset::getPropertyNames() const {
    std::vector<std::string> result;
    result.reserve(property_name_ids.size());
    for (const int name_id : property_name_ids) {
        result.push_back(resolveName(name_id));
    }
    return result;
}

std::vector<std::string> ScriptAsset::getGlobalNames() const {
    std::vector<std::string> result;
    result.reserve(global_name_ids.size());
    for (const int name_id : global_name_ids) {
        result.push_back(resolveName(name_id));
    }
    return result;
}

std::string ScriptAsset::displayName() const {
    const auto type_name = format::getScriptTypeName(static_cast<chunks::ScriptChunkType>(type));
    if (!name.empty()) {
        return "\"" + name + "\" (" + type_name + ")";
    }
    return type_name + " #" + std::to_string(chunk_id);
}

} // namespace libreshockwave::moviesource
