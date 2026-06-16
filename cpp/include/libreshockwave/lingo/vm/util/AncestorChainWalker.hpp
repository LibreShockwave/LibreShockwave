#pragma once

#include <memory>
#include <string_view>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::vm::util {

inline constexpr int MAX_ANCESTOR_DEPTH = 100;

[[nodiscard]] Datum getProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] bool hasProperty(const Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] Datum::ScriptInstanceRef* findOwner(Datum::ScriptInstanceRef& instance, std::string_view propName);
[[nodiscard]] const Datum::ScriptInstanceRef* findOwner(const Datum::ScriptInstanceRef& instance,
                                                        std::string_view propName);
[[nodiscard]] std::shared_ptr<Datum::ScriptInstanceRef> getAncestorAtDepth(
    const Datum::ScriptInstanceRef& instance,
    int depth);
void setProperty(Datum::ScriptInstanceRef& instance, std::string_view propName, Datum value);
[[nodiscard]] int getAncestorDepth(const Datum::ScriptInstanceRef& instance);

} // namespace libreshockwave::lingo::vm::util
