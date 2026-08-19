#include "SymbolIndex.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace libreshockwave::debugger {

namespace {

DeclarationTarget makeTarget(int castLibNumber,
                             const MovieTreeSnapshot::ScriptEntry& script,
                             const std::string& name, DeclarationKind kind) {
    DeclarationTarget target;
    target.castLibNumber = castLibNumber;
    target.scriptId = script.scriptId;
    target.scriptName = script.displayName;
    target.kind = kind;
    if (kind == DeclarationKind::Method) {
        target.handlerName = name;
    }
    return target;
}

} // namespace

SymbolIndex::SymbolIndex(const MovieTreeSnapshot& snapshot) {
    std::unordered_set<std::string> seenMethods;
    std::unordered_set<std::string> seenDeclarations;
    const auto addDeclaration = [&seenDeclarations, this](const std::string& key) {
        if (seenDeclarations.insert(key).second) {
            declarationNames_.push_back(key);
        }
    };
    for (const auto& movie : snapshot.movies) {
        for (const auto& script : movie.scripts) {
            for (const auto& handler : script.handlers) {
                if (handler.name.empty()) continue;
                const std::string key = keyFor(handler.name);
                byName_[key].push_back(
                    makeTarget(movie.castLibNumber, script, handler.name,
                               DeclarationKind::Method));
                if (seenMethods.insert(key).second) {
                    methodNames_.push_back(key);
                }
                addDeclaration(key);
            }
            for (const auto& property : script.propertyNames) {
                if (property.empty()) continue;
                const std::string key = keyFor(property);
                byName_[key].push_back(makeTarget(
                    movie.castLibNumber, script, property,
                    DeclarationKind::Property));
                addDeclaration(key);
            }
            for (const auto& global : script.globalNames) {
                if (global.empty()) continue;
                const std::string key = keyFor(global);
                byName_[key].push_back(
                    makeTarget(movie.castLibNumber, script, global,
                               DeclarationKind::Global));
                addDeclaration(key);
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
                                                 int preferScriptId) const {
    const auto it = byName_.find(keyFor(name));
    if (it == byName_.end()) {
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
                          [preferScriptId](const DeclarationTarget& target) {
                              return target.scriptId == preferScriptId;
                          });

    std::vector<DeclarationTarget> result;
    result.reserve(methods.size() + properties.size() + globals.size());
    for (const auto& target : methods) result.push_back(target);
    for (const auto& target : properties) result.push_back(target);
    for (const auto& target : globals) result.push_back(target);
    return result;
}

const std::vector<std::string>& SymbolIndex::methodNames() const {
    return methodNames_;
}

const std::vector<std::string>& SymbolIndex::declarationNames() const {
    return declarationNames_;
}

bool SymbolIndex::empty() const {
    return byName_.empty();
}

} // namespace libreshockwave::debugger
