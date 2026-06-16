#pragma once

#include <string_view>

namespace libreshockwave::lingo {

enum class Opcode {
    INVALID = 0x00,
    RET = 0x01,
    RET_FACTORY = 0x02,
    PUSH_ZERO = 0x03,
    MUL = 0x04,
    ADD = 0x05,
    SUB = 0x06,
    DIV = 0x07,
    MOD = 0x08,
    INV = 0x09,
    JOIN_STR = 0x0A,
    JOIN_PAD_STR = 0x0B,
    LT = 0x0C,
    LT_EQ = 0x0D,
    NT_EQ = 0x0E,
    EQ = 0x0F,
    GT = 0x10,
    GT_EQ = 0x11,
    AND = 0x12,
    OR = 0x13,
    NOT = 0x14,
    CONTAINS_STR = 0x15,
    CONTAINS_0_STR = 0x16,
    GET_CHUNK = 0x17,
    HILITE_CHUNK = 0x18,
    ONTO_SPR = 0x19,
    INTO_SPR = 0x1A,
    GET_FIELD = 0x1B,
    START_TELL = 0x1C,
    END_TELL = 0x1D,
    PUSH_LIST = 0x1E,
    PUSH_PROP_LIST = 0x1F,
    SWAP = 0x21,
    CALL_JAVASCRIPT = 0x26,
    PUSH_INT8 = 0x41,
    PUSH_ARG_LIST_NO_RET = 0x42,
    PUSH_ARG_LIST = 0x43,
    PUSH_CONS = 0x44,
    PUSH_SYMB = 0x45,
    PUSH_VAR_REF = 0x46,
    GET_GLOBAL2 = 0x48,
    GET_GLOBAL = 0x49,
    GET_PROP = 0x4A,
    GET_PARAM = 0x4B,
    GET_LOCAL = 0x4C,
    SET_GLOBAL2 = 0x4E,
    SET_GLOBAL = 0x4F,
    SET_PROP = 0x50,
    SET_PARAM = 0x51,
    SET_LOCAL = 0x52,
    JMP = 0x53,
    END_REPEAT = 0x54,
    JMP_IF_Z = 0x55,
    LOCAL_CALL = 0x56,
    EXT_CALL = 0x57,
    OBJ_CALL_V4 = 0x58,
    PUT = 0x59,
    PUT_CHUNK = 0x5A,
    DELETE_CHUNK = 0x5B,
    GET = 0x5C,
    SET = 0x5D,
    GET_MOVIE_PROP = 0x5F,
    SET_MOVIE_PROP = 0x60,
    GET_OBJ_PROP = 0x61,
    SET_OBJ_PROP = 0x62,
    TELL_CALL = 0x63,
    PEEK = 0x64,
    POP = 0x65,
    THE_BUILTIN = 0x66,
    OBJ_CALL = 0x67,
    PUSH_CHUNK_VAR_REF = 0x6D,
    PUSH_INT16 = 0x6E,
    PUSH_INT32 = 0x6F,
    GET_CHAINED_PROP = 0x70,
    PUSH_FLOAT32 = 0x71,
    GET_TOP_LEVEL_PROP = 0x72,
    NEW_OBJ = 0x73
};

[[nodiscard]] int code(Opcode opcode);
[[nodiscard]] std::string_view mnemonic(Opcode opcode);
[[nodiscard]] int argBytes(Opcode opcode);
[[nodiscard]] Opcode opcodeFromCode(int code);
[[nodiscard]] bool isSingleByte(Opcode opcode);
[[nodiscard]] bool isMultiByte(Opcode opcode);
[[nodiscard]] bool isJump(Opcode opcode);
[[nodiscard]] bool isCall(Opcode opcode);
[[nodiscard]] bool isReturn(Opcode opcode);
[[nodiscard]] bool isPush(Opcode opcode);

} // namespace libreshockwave::lingo
