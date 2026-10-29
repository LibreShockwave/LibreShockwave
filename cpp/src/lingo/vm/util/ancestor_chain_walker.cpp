#include "libreshockwave/lingo/vm/util/ancestor_chain_walker.hpp"

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

const Datum* propertyValue(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    const int index = instance.findPropertyIndex(prop_name);
    return index >= 0 ? &instance.properties()[static_cast<std::size_t>(index)].second : nullptr;
}

Datum* propertyValue(Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    const int index = instance.findPropertyIndex(prop_name);
    return index >= 0 ? &instance.properties()[static_cast<std::size_t>(index)].second : nullptr;
}

template <typename Instance>
Instance* findScriptNodeImpl(Instance& instance,
                             int script_chunk_id,
                             const ScriptChunkIdLookup& resolve_chunk_id) {
    auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& script_ref = current->scriptRef(); script_ref.has_value()) {
            const int cast_lib = script_ref->cast_lib > 0 ? script_ref->cast_lib : 1;
            if (resolve_chunk_id(cast_lib, script_ref->memberNum()) == script_chunk_id) {
                return current;
            }
        }

        current = current->ancestorRaw();
    }
    return nullptr;
}

} // namespace

Datum::ScriptInstanceRef* findScriptNode(Datum::ScriptInstanceRef& instance,
                                         int script_chunk_id,
                                         const ScriptChunkIdLookup& resolve_chunk_id) {
    return findScriptNodeImpl(instance, script_chunk_id, resolve_chunk_id);
}

const Datum::ScriptInstanceRef* findScriptNode(const Datum::ScriptInstanceRef& instance,
                                               int script_chunk_id,
                                               const ScriptChunkIdLookup& resolve_chunk_id) {
    return findScriptNodeImpl(instance, script_chunk_id, resolve_chunk_id);
}

Datum getProperty(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        if (auto ancestor = instance.ancestor()) {
            return Datum::scriptInstanceRef(std::move(ancestor));
        }
        return Datum::voidValue();
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto* value = propertyValue(*current, prop_name)) {
            return *value;
        }

        current = current->ancestorRaw();
    }

    return Datum::voidValue();
}

const Datum* findPropertyValue(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return nullptr;
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto* value = propertyValue(*current, prop_name)) {
            return value;
        }

        current = current->ancestorRaw();
    }

    return nullptr;
}

bool hasProperty(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return instance.ancestor() != nullptr;
    }
    return findOwner(instance, prop_name) != nullptr;
}

Datum::ScriptInstanceRef* findOwner(Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return instance.ancestor() != nullptr ? &instance : nullptr;
    }

    auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (propertyValue(*current, prop_name) != nullptr) {
            return current;
        }

        current = current->ancestorRaw();
    }

    return nullptr;
}

const Datum::ScriptInstanceRef* findOwner(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return instance.ancestor() != nullptr ? &instance : nullptr;
    }

    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (propertyValue(*current, prop_name) != nullptr) {
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
    const Datum::ScriptInstanceRef* current_raw = &instance;
    for (int index = 0; index < depth && index < MAX_ANCESTOR_DEPTH; ++index) {
        current = current_raw->ancestor();
        if (!current) {
            return nullptr;
        }
        current_raw = current.get();
    }
    return current;
}

void setProperty(Datum::ScriptInstanceRef& instance, std::string_view prop_name, Datum value) {
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        if (value.type() == DatumType::ScriptInstanceRef) {
            instance.setAncestor(value.scriptInstancePtr());
        }
        return;
    }

    auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < MAX_ANCESTOR_DEPTH; ++depth) {
        if (auto* actual_value = propertyValue(*current, prop_name)) {
            *actual_value = std::move(value);
            return;
        }

        current = current->ancestorRaw();
    }

    instance.setProperty(std::string(prop_name), std::move(value));
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

} // namespace libreshockwave::lingo::vm::util
