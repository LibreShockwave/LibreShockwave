#pragma once

#include <QString>

namespace libreshockwave::lingo {
class Datum;
}

namespace libreshockwave::debugger {

/// Utilities for rendering Datum values as display strings in the Qt UI.
namespace DatumFormat {

/// Returns a human-readable string representation of a Datum value.
[[nodiscard]] QString toDisplayString(const lingo::Datum& value);

/// Returns a short type name for the datum (e.g. "Integer", "String", "List").
[[nodiscard]] QString typeName(const lingo::Datum& value);

/// Returns whether the datum can be expanded (list, propList, scriptInstance).
[[nodiscard]] bool isExpandable(const lingo::Datum& value);

} // namespace DatumFormat

} // namespace libreshockwave::debugger
