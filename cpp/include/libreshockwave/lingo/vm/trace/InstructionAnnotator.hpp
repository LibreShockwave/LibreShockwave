#pragma once

#include <string>

#include "libreshockwave/chunks/ScriptChunk.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::vm::trace {

class InstructionAnnotator {
public:
    InstructionAnnotator() = delete;

    [[nodiscard]] static std::string annotate(const chunks::ScriptChunk& script,
                                              const chunks::ScriptChunk::Handler* handler,
                                              const chunks::ScriptChunk::Instruction& instruction,
                                              const chunks::ScriptNamesChunk* names,
                                              bool resolveNames);
    [[nodiscard]] static std::string annotate(const chunks::ScriptChunk& script,
                                              const chunks::ScriptChunk::Instruction& instruction,
                                              const chunks::ScriptNamesChunk* names = nullptr);
};

} // namespace libreshockwave::lingo::vm::trace
