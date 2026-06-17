#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

#include "libreshockwave/lingo/Opcode.hpp"
#include "libreshockwave/lingo/vm/ExecutionContext.hpp"

namespace libreshockwave::lingo::vm {

using OpcodeHandler = std::function<bool(ExecutionContext& context)>;

class OpcodeRegistry {
public:
    OpcodeRegistry();

    [[nodiscard]] const OpcodeHandler* get(Opcode opcode) const;
    [[nodiscard]] bool hasHandler(Opcode opcode) const;
    bool execute(Opcode opcode, ExecutionContext& context) const;
    void registerHandler(Opcode opcode, OpcodeHandler handler);

private:
    static constexpr std::size_t kHandlerCount = 0x74;
    std::array<OpcodeHandler, kHandlerCount> handlers_{};
};

class StackOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class ControlFlowOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class ArithmeticOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class ComparisonOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class LogicalOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class StringOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class VariableOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class ListOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class PropertyOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

class CallOpcodes {
public:
    static void registerHandlers(OpcodeRegistry& registry);
};

[[nodiscard]] std::string imageOperationTraceJson();
void clearImageOperationTrace();

} // namespace libreshockwave::lingo::vm
