#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::moviesource {

// Compiles one .lsw script text into a ScriptAsset with the lingo/compiler
// pipeline. `names` is the name table from the manifest; when it is empty a
// table is built from the token order.
//
// Returns nullptr and fills `error` when the text fails to lex, parse, or
// compile. The loader treats that as a fatal load error.
[[nodiscard]] std::shared_ptr<ScriptAsset> compileLswScript(const std::string& script_name,
                                                            const std::string& text,
                                                            const std::vector<std::string>& names,
                                                            ScriptType type,
                                                            int behavior_flags,
                                                            int version,
                                                            bool capital_x,
                                                            std::string& error);

} // namespace libreshockwave::moviesource
