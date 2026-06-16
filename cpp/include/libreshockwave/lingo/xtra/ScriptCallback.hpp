#pragma once

#include <functional>
#include <string>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::xtra {

using ScriptCallback = std::function<void(const Datum& target,
                                          const std::string& handlerName,
                                          const std::vector<Datum>& args)>;

} // namespace libreshockwave::lingo::xtra
