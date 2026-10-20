#pragma once

#include <string>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::lingo::vm::datum {

[[nodiscard]] std::string format(const Datum& value, int max_string_length = 50);
[[nodiscard]] std::string format(const Datum* value, int max_string_length = 50);
[[nodiscard]] std::string formatWithType(const Datum& value);
[[nodiscard]] std::string formatWithType(const Datum* value);
[[nodiscard]] std::string formatBrief(const Datum& value);
[[nodiscard]] std::string formatBrief(const Datum* value);
[[nodiscard]] std::string formatExpanded(const Datum& value);
[[nodiscard]] std::string formatExpanded(const Datum* value);
[[nodiscard]] std::string formatDetailed(const Datum& value, int indent = 0);
[[nodiscard]] std::string formatDetailed(const Datum* value, int indent = 0);
[[nodiscard]] std::string getTypeName(const Datum& value);
[[nodiscard]] std::string getTypeName(const Datum* value);

} // namespace libreshockwave::lingo::vm::datum
