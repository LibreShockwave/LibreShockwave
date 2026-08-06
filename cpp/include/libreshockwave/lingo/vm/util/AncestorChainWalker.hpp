#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::chunks {
class ScriptChunk;
}

namespace libreshockwave::lingo::vm::util {

inline constexpr int MAX_ANCESTOR_DEPTH = 100;

[[nodiscard]] Datum getProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] const Datum* findPropertyValue(const Datum::ScriptInstanceRef& instance,
                                             std::string_view propName);
[[nodiscard]] bool hasProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] Datum::ScriptInstanceRef* findOwner(Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] const Datum::ScriptInstanceRef* findOwner(const Datum::ScriptInstanceRef& instance,
                                                        std::string_view propName);
[[nodiscard]] std::shared_ptr<Datum::ScriptInstanceRef> getAncestorAtDepth(
    const Datum::ScriptInstanceRef& instance,
    int depth);
void setProperty(Datum::ScriptInstanceRef& instance, std::string_view propName, Datum value);
[[nodiscard]] int getAncestorDepth(const Datum::ScriptInstanceRef& instance);

/// Walk the receiver's ancestor chain to find the instance whose script
/// matches the handler's script. This is the "handler's owning instance"
/// — the correct target for bare property access (getProp/setProp) in
/// Director's prototypal inheritance model.
///
/// When a handler from an ancestor script runs with a child instance as
/// receiver, bare `me.prop` must resolve against the handler's own
/// instance level — e.g. `ancestor.getMember()` should resolve `ancestor`
/// relative to the handler's own instance, not the receiver.
///
/// The returned shared_ptr shares ownership with the ancestor chain, so
/// modifications through it affect the original instance.
///
/// Falls back to the receiver if the handler's script is not found in
/// the chain (this happens when the handler is defined on the receiver's
/// own script, or when the chain is broken).
///
/// Matches the reference VM's handler-level instance resolution for getProp/setProp.
[[nodiscard]] std::shared_ptr<Datum::ScriptInstanceRef> findHandlerLevelInstance(
    std::shared_ptr<Datum::ScriptInstanceRef> receiver,
    int handlerScriptChunkId,
    const std::function<int(int castLib, int memberNum)>& scriptChunkIdResolver);

/// Walk the ancestor chain looking for a non-ScriptInstanceRef ancestor
/// stored in properties["ancestor"]. This is used for delegation to
/// non-script objects like TimeoutInstance.
///
/// For each instance in the chain, checks:
/// 1. properties["ancestor"] → ScriptInstanceRef: keep walking
/// 2. properties["ancestor"] → Void / Int(0): continue to struct field
/// 3. properties["ancestor"] → anything else: return it (delegation target)
/// 4. Then check the struct ancestor field to continue the walk
///
/// Returns the Datum from properties["ancestor"] if a non-instance
/// delegation target is found, or nullptr if none exists.
///
/// Matches the reference VM's non-script-instance ancestor delegation walk.
[[nodiscard]] const Datum* findNonScriptAncestor(const Datum::ScriptInstanceRef& instance);

} // namespace libreshockwave::lingo::vm::util
