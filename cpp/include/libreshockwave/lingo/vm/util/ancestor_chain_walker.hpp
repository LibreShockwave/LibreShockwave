#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::lingo::vm::util {

inline constexpr int MAX_ANCESTOR_DEPTH = 100;

// Resolves a cast member reference to the id of its script chunk, or -1.
using ScriptChunkIdLookup = std::function<int(int cast_lib, int member_num)>;

// Returns the ancestor-chain node that was created from the script chunk with
// the given chunk id. A handler uses this node for its own property storage so
// a child script that declares the same property name cannot hide the parent's
// property from the parent's handlers. Returns null when no node matches.
[[nodiscard]] Datum::ScriptInstanceRef* findScriptNode(Datum::ScriptInstanceRef& instance,
                                                       int script_chunk_id,
                                                       const ScriptChunkIdLookup& resolve_chunk_id);
[[nodiscard]] const Datum::ScriptInstanceRef* findScriptNode(const Datum::ScriptInstanceRef& instance,
                                                             int script_chunk_id,
                                                             const ScriptChunkIdLookup& resolve_chunk_id);

[[nodiscard]] Datum getProperty(const Datum::ScriptInstanceRef& instance, std::string_view prop_name);
[[nodiscard]] const Datum* findPropertyValue(const Datum::ScriptInstanceRef& instance,
                                             std::string_view prop_name);
[[nodiscard]] bool hasProperty(const Datum::ScriptInstanceRef& instance, std::string_view prop_name);
[[nodiscard]] Datum::ScriptInstanceRef* findOwner(Datum::ScriptInstanceRef& instance, std::string_view prop_name);
[[nodiscard]] const Datum::ScriptInstanceRef* findOwner(const Datum::ScriptInstanceRef& instance,
                                                        std::string_view prop_name);
[[nodiscard]] std::shared_ptr<Datum::ScriptInstanceRef> getAncestorAtDepth(
    const Datum::ScriptInstanceRef& instance,
    int depth);
void setProperty(Datum::ScriptInstanceRef& instance, std::string_view prop_name, Datum value);
[[nodiscard]] int getAncestorDepth(const Datum::ScriptInstanceRef& instance);

} // namespace libreshockwave::lingo::vm::util
