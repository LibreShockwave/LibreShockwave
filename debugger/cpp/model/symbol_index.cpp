#include "symbol_index.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace libreshockwave::debugger {

namespace {

DeclarationTarget makeTarget(int cast_lib_number,
                             const MovieTreeSnapshot::ScriptEntry& script,
                             const std::string& name, DeclarationKind kind) {
    DeclarationTarget target;
    target.cast_lib_number = cast_lib_number;
    target.script_id = script.script_id;
    target.script_name = script.display_name;
    target.kind = kind;
    if (kind == DeclarationKind::Method) {
        target.handler_name = name;
    }
    return target;
}

} // namespace

SymbolIndex::SymbolIndex(const MovieTreeSnapshot& snapshot) {
    std::unordered_set<std::string> seen_methods;
    std::unordered_set<std::string> seen_declarations;
    const auto add_declaration = [&seen_declarations, this](const std::string& key) {
        if (seen_declarations.insert(key).second) {
            declaration_names_.push_back(key);
        }
    };
    for (const auto& movie : snapshot.movies) {
        for (const auto& script : movie.scripts) {
            for (const auto& handler : script.handlers) {
                if (handler.name.empty()) continue;
                const std::string key = keyFor(handler.name);
                by_name_[key].push_back(
                    makeTarget(movie.cast_lib_number, script, handler.name,
                               DeclarationKind::Method));
                if (seen_methods.insert(key).second) {
                    method_names_.push_back(key);
                }
                add_declaration(key);
            }
            for (const auto& property : script.property_names) {
                if (property.empty()) continue;
                const std::string key = keyFor(property);
                by_name_[key].push_back(makeTarget(
                    movie.cast_lib_number, script, property,
                    DeclarationKind::Property));
                add_declaration(key);
            }
            for (const auto& global : script.global_names) {
                if (global.empty()) continue;
                const std::string key = keyFor(global);
                by_name_[key].push_back(
                    makeTarget(movie.cast_lib_number, script, global,
                               DeclarationKind::Global));
                add_declaration(key);
            }
        }
    }
}

std::string SymbolIndex::keyFor(const std::string& name) {
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return key;
}

std::vector<DeclarationTarget> SymbolIndex::find(const std::string& name,
                                                 int prefer_script_id) const {
    const auto it = by_name_.find(keyFor(name));
    if (it == by_name_.end()) {
        return {};
    }

    std::vector<DeclarationTarget> methods;
    std::vector<DeclarationTarget> properties;
    std::vector<DeclarationTarget> globals;
    for (const auto& target : it->second) {
        switch (target.kind) {
            case DeclarationKind::Method:
                methods.push_back(target);
                break;
            case DeclarationKind::Property:
                properties.push_back(target);
                break;
            case DeclarationKind::Global:
                globals.push_back(target);
                break;
        }
    }

    // Keep snapshot order (cast library, then script) but float the preferred
    // script's methods to the front.
    std::stable_partition(methods.begin(), methods.end(),
                          [prefer_script_id](const DeclarationTarget& target) {
                              return target.script_id == prefer_script_id;
                          });

    std::vector<DeclarationTarget> result;
    result.reserve(methods.size() + properties.size() + globals.size());
    for (const auto& target : methods) result.push_back(target);
    for (const auto& target : properties) result.push_back(target);
    for (const auto& target : globals) result.push_back(target);
    return result;
}

const std::vector<std::string>& SymbolIndex::methodNames() const {
    return method_names_;
}

const std::vector<std::string>& SymbolIndex::declarationNames() const {
    return declaration_names_;
}

bool SymbolIndex::empty() const {
    return by_name_.empty();
}

} // namespace libreshockwave::debugger
