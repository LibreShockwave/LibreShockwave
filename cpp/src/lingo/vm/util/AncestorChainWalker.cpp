#include "libreshockwave/lingo/vm/util/AncestorChainWalker.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace libreshockwave::lingo::vm::util {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

const Datum* propertyValue(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    const int index = instance.findPropertyIndex(propName);
    return index >= 0 ? &instance.properties()[static_cast<std::size_t>(index)].second : nullptr;
}

Datum* propertyValue(Datum::ScriptInstanceRef& instance, std::string_view propName) {
    const int index = instance.findPropertyIndex(propName);
    return index >= 0 ? &instance.properties()[static_cast<std::size_t>(index)].second : nullptr;
}

} // namespace

Datum getProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        if (auto ancestor = instance.ancestor()) {
            return Datum::scriptInstanceRef(std::move(ancestor));
        }
        return Datum::voidValue();
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto* value = propertyValue(*current, propName)) {
            return *value;
        }

        current = current->ancestorRaw();
    }

    return Datum::voidValue();
}

const Datum* findPropertyValue(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        return nullptr;
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto* value = propertyValue(*current, propName)) {
            return value;
        }

        current = current->ancestorRaw();
    }

    return nullptr;
}

bool hasProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        return instance.ancestor() != nullptr;
    }
    return findOwner(instance, propName) != nullptr;
}

Datum::ScriptInstanceRef* findOwner(Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        return instance.ancestor() != nullptr ? &instance : nullptr;
    }

    auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (propertyValue(*current, propName) != nullptr) {
            return current;
        }

        current = current->ancestorRaw();
    }

    return nullptr;
}

const Datum::ScriptInstanceRef* findOwner(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        return instance.ancestor() != nullptr ? &instance : nullptr;
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (propertyValue(*current, propName) != nullptr) {
            return current;
        }

        current = current->ancestorRaw();
    }

    return nullptr;
}

std::shared_ptr<Datum::ScriptInstanceRef> getAncestorAtDepth(const Datum::ScriptInstanceRef& instance, int depth) {
    if (depth < 1) {
        return nullptr;
    }

    std::shared_ptr<Datum::ScriptInstanceRef> current;
    const Datum::ScriptInstanceRef* currentRaw = &instance;
    for (int index = 0; index < depth && index < MAX_ANCESTOR_DEPTH; ++index) {
        current = currentRaw->ancestor();
        if (!current) {
            return nullptr;
        }
        currentRaw = current.get();
    }
    return current;
}

void setProperty(Datum::ScriptInstanceRef& instance, std::string_view propName, Datum value) {
    if (equalsIgnoreCase(propName, "ancestor")) {
        // Coerce to ScriptInstanceRef,
        // error on non-instance values (including Void).
        if (value.type() == DatumType::ScriptInstanceRef) {
            instance.setAncestor(value.scriptInstancePtr());
            return;
        }
        throw LingoException("Cannot set ancestor to non-ScriptInstanceRef value");
    }

    auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (auto* actualValue = propertyValue(*current, propName)) {
            *actualValue = std::move(value);
            return;
        }

        current = current->ancestorRaw();
    }

    instance.setProperty(std::string(propName), std::move(value));
}

int getAncestorDepth(const Datum::ScriptInstanceRef& instance) {
    int depth = 0;
    const auto* current = &instance;
    for (int index = 0; current != nullptr && index < MAX_ANCESTOR_DEPTH; ++index) {
        current = current->ancestorRaw();
        if (current == nullptr) {
            break;
        }
        ++depth;
    }
    return depth;
}

std::shared_ptr<Datum::ScriptInstanceRef> findHandlerLevelInstance(
    std::shared_ptr<Datum::ScriptInstanceRef> receiver,
    const int handlerScriptChunkId,
    const std::function<int(int, int)>& scriptChunkIdResolver) {
    // Walk the receiver's ancestor chain. The receiver parameter is a
    // shared_ptr that shares ownership with the Datum holding it, so the
    // returned shared_ptr at any depth shares ownership with the chain.
    std::shared_ptr<Datum::ScriptInstanceRef> current = std::move(receiver);
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& scriptRef = current->scriptRef(); scriptRef.has_value()) {
            const int castLib = scriptRef->castLib > 0 ? scriptRef->castLib : 1;
            const int scriptChunkId = scriptChunkIdResolver(castLib, scriptRef->memberNum());
            if (scriptChunkId == handlerScriptChunkId) {
                return current;
            }
        }
        auto ancestor = current->ancestor();
        current = std::move(ancestor);
    }

    // Fallback: return a copy of the receiver.
    // Note: the caller must have a fallback shared_ptr for this case.
    return nullptr;
}

const Datum* findNonScriptAncestor(const Datum::ScriptInstanceRef& instance) {
    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        // Check if this instance has a non-ScriptInstanceRef ancestor in properties
        const int propIndex = current->findPropertyIndex("ancestor");
        if (propIndex >= 0) {
            const auto& ancestorProp = current->properties()[static_cast<std::size_t>(propIndex)].second;
            if (ancestorProp.type() == DatumType::ScriptInstanceRef) {
                // ScriptInstanceRef ancestor in properties: keep walking
                // (this was set via setAt with non-instance followed by setAt with instance,
                //  or it's a legacy artifact — treat the properties entry as the chain link)
                const auto* next = ancestorProp.scriptInstancePtr().get();
                current = next;
                continue;
            }
            if (ancestorProp.isVoid() || (ancestorProp.isInt() && ancestorProp.intValue() == 0)) {
                // Void or Int(0) in properties: fall through to check struct field
            } else {
                // Non-ScriptInstanceRef ancestor found (e.g., TimeoutInstance)
                return &ancestorProp;
            }
        }

        // Check the struct ancestor field for ScriptInstanceRef ancestors
        auto ancestor = current->ancestor();
        current = ancestor ? ancestor.get() : nullptr;
    }

    return nullptr;
}

} // namespace libreshockwave::lingo::vm::util
