#pragma once

#include <string>

#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::lingo::vm::trace {

class InstructionAnnotator {
public:
    InstructionAnnotator() = delete;

    [[nodiscard]] static std::string annotate(const moviesource::ScriptAsset& script,
                                              const moviesource::ScriptAsset::Handler* handler,
                                              const moviesource::ScriptAsset::Instruction& instruction,
                                              bool resolve_names);
    [[nodiscard]] static std::string annotate(const moviesource::ScriptAsset& script,
                                              const moviesource::ScriptAsset::Instruction& instruction);
};

} // namespace libreshockwave::lingo::vm::trace
