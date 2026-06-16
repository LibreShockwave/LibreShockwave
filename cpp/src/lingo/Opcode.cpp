#include "libreshockwave/lingo/Opcode.hpp"

namespace libreshockwave::lingo {

int code(Opcode opcode) {
    return static_cast<int>(opcode);
}

std::string_view mnemonic(Opcode opcode) {
    switch (opcode) {
        case Opcode::INVALID: return "invalid";
        case Opcode::RET: return "ret";
        case Opcode::RET_FACTORY: return "retFactory";
        case Opcode::PUSH_ZERO: return "pushZero";
        case Opcode::MUL: return "mul";
        case Opcode::ADD: return "add";
        case Opcode::SUB: return "sub";
        case Opcode::DIV: return "div";
        case Opcode::MOD: return "mod";
        case Opcode::INV: return "inv";
        case Opcode::JOIN_STR: return "joinStr";
        case Opcode::JOIN_PAD_STR: return "joinPadStr";
        case Opcode::LT: return "lt";
        case Opcode::LT_EQ: return "ltEq";
        case Opcode::NT_EQ: return "ntEq";
        case Opcode::EQ: return "eq";
        case Opcode::GT: return "gt";
        case Opcode::GT_EQ: return "gtEq";
        case Opcode::AND: return "and";
        case Opcode::OR: return "or";
        case Opcode::NOT: return "not";
        case Opcode::CONTAINS_STR: return "containsStr";
        case Opcode::CONTAINS_0_STR: return "contains0Str";
        case Opcode::GET_CHUNK: return "getChunk";
        case Opcode::HILITE_CHUNK: return "hiliteChunk";
        case Opcode::ONTO_SPR: return "ontoSpr";
        case Opcode::INTO_SPR: return "intoSpr";
        case Opcode::GET_FIELD: return "getField";
        case Opcode::START_TELL: return "startTell";
        case Opcode::END_TELL: return "endTell";
        case Opcode::PUSH_LIST: return "pushList";
        case Opcode::PUSH_PROP_LIST: return "pushPropList";
        case Opcode::SWAP: return "swap";
        case Opcode::CALL_JAVASCRIPT: return "callJavaScript";
        case Opcode::PUSH_INT8: return "pushInt8";
        case Opcode::PUSH_ARG_LIST_NO_RET: return "pushArgListNoRet";
        case Opcode::PUSH_ARG_LIST: return "pushArgList";
        case Opcode::PUSH_CONS: return "pushCons";
        case Opcode::PUSH_SYMB: return "pushSymb";
        case Opcode::PUSH_VAR_REF: return "pushVarRef";
        case Opcode::GET_GLOBAL2: return "getGlobal2";
        case Opcode::GET_GLOBAL: return "getGlobal";
        case Opcode::GET_PROP: return "getProp";
        case Opcode::GET_PARAM: return "getParam";
        case Opcode::GET_LOCAL: return "getLocal";
        case Opcode::SET_GLOBAL2: return "setGlobal2";
        case Opcode::SET_GLOBAL: return "setGlobal";
        case Opcode::SET_PROP: return "setProp";
        case Opcode::SET_PARAM: return "setParam";
        case Opcode::SET_LOCAL: return "setLocal";
        case Opcode::JMP: return "jmp";
        case Opcode::END_REPEAT: return "endRepeat";
        case Opcode::JMP_IF_Z: return "jmpIfZ";
        case Opcode::LOCAL_CALL: return "localCall";
        case Opcode::EXT_CALL: return "extCall";
        case Opcode::OBJ_CALL_V4: return "objCallV4";
        case Opcode::PUT: return "put";
        case Opcode::PUT_CHUNK: return "putChunk";
        case Opcode::DELETE_CHUNK: return "deleteChunk";
        case Opcode::GET: return "get";
        case Opcode::SET: return "set";
        case Opcode::GET_MOVIE_PROP: return "getMovieProp";
        case Opcode::SET_MOVIE_PROP: return "setMovieProp";
        case Opcode::GET_OBJ_PROP: return "getObjProp";
        case Opcode::SET_OBJ_PROP: return "setObjProp";
        case Opcode::TELL_CALL: return "tellCall";
        case Opcode::PEEK: return "peek";
        case Opcode::POP: return "pop";
        case Opcode::THE_BUILTIN: return "theBuiltin";
        case Opcode::OBJ_CALL: return "objCall";
        case Opcode::PUSH_CHUNK_VAR_REF: return "pushChunkVarRef";
        case Opcode::PUSH_INT16: return "pushInt16";
        case Opcode::PUSH_INT32: return "pushInt32";
        case Opcode::GET_CHAINED_PROP: return "getChainedProp";
        case Opcode::PUSH_FLOAT32: return "pushFloat32";
        case Opcode::GET_TOP_LEVEL_PROP: return "getTopLevelProp";
        case Opcode::NEW_OBJ: return "newObj";
    }
    return "invalid";
}

int argBytes(Opcode opcode) {
    switch (opcode) {
        case Opcode::PUSH_INT16:
            return 2;
        case Opcode::PUSH_INT32:
        case Opcode::PUSH_FLOAT32:
            return 4;
        case Opcode::PUSH_INT8:
        case Opcode::PUSH_ARG_LIST_NO_RET:
        case Opcode::PUSH_ARG_LIST:
        case Opcode::PUSH_CONS:
        case Opcode::PUSH_SYMB:
        case Opcode::PUSH_VAR_REF:
        case Opcode::GET_GLOBAL2:
        case Opcode::GET_GLOBAL:
        case Opcode::GET_PROP:
        case Opcode::GET_PARAM:
        case Opcode::GET_LOCAL:
        case Opcode::SET_GLOBAL2:
        case Opcode::SET_GLOBAL:
        case Opcode::SET_PROP:
        case Opcode::SET_PARAM:
        case Opcode::SET_LOCAL:
        case Opcode::JMP:
        case Opcode::END_REPEAT:
        case Opcode::JMP_IF_Z:
        case Opcode::LOCAL_CALL:
        case Opcode::EXT_CALL:
        case Opcode::OBJ_CALL_V4:
        case Opcode::PUT:
        case Opcode::PUT_CHUNK:
        case Opcode::DELETE_CHUNK:
        case Opcode::GET:
        case Opcode::SET:
        case Opcode::GET_MOVIE_PROP:
        case Opcode::SET_MOVIE_PROP:
        case Opcode::GET_OBJ_PROP:
        case Opcode::SET_OBJ_PROP:
        case Opcode::TELL_CALL:
        case Opcode::PEEK:
        case Opcode::POP:
        case Opcode::THE_BUILTIN:
        case Opcode::OBJ_CALL:
        case Opcode::PUSH_CHUNK_VAR_REF:
        case Opcode::GET_CHAINED_PROP:
        case Opcode::GET_TOP_LEVEL_PROP:
        case Opcode::NEW_OBJ:
            return 1;
        default:
            return 0;
    }
}

Opcode opcodeFromCode(int value) {
    switch (value) {
        case 0x00: return Opcode::INVALID;
        case 0x01: return Opcode::RET;
        case 0x02: return Opcode::RET_FACTORY;
        case 0x03: return Opcode::PUSH_ZERO;
        case 0x04: return Opcode::MUL;
        case 0x05: return Opcode::ADD;
        case 0x06: return Opcode::SUB;
        case 0x07: return Opcode::DIV;
        case 0x08: return Opcode::MOD;
        case 0x09: return Opcode::INV;
        case 0x0A: return Opcode::JOIN_STR;
        case 0x0B: return Opcode::JOIN_PAD_STR;
        case 0x0C: return Opcode::LT;
        case 0x0D: return Opcode::LT_EQ;
        case 0x0E: return Opcode::NT_EQ;
        case 0x0F: return Opcode::EQ;
        case 0x10: return Opcode::GT;
        case 0x11: return Opcode::GT_EQ;
        case 0x12: return Opcode::AND;
        case 0x13: return Opcode::OR;
        case 0x14: return Opcode::NOT;
        case 0x15: return Opcode::CONTAINS_STR;
        case 0x16: return Opcode::CONTAINS_0_STR;
        case 0x17: return Opcode::GET_CHUNK;
        case 0x18: return Opcode::HILITE_CHUNK;
        case 0x19: return Opcode::ONTO_SPR;
        case 0x1A: return Opcode::INTO_SPR;
        case 0x1B: return Opcode::GET_FIELD;
        case 0x1C: return Opcode::START_TELL;
        case 0x1D: return Opcode::END_TELL;
        case 0x1E: return Opcode::PUSH_LIST;
        case 0x1F: return Opcode::PUSH_PROP_LIST;
        case 0x21: return Opcode::SWAP;
        case 0x26: return Opcode::CALL_JAVASCRIPT;
        case 0x41: return Opcode::PUSH_INT8;
        case 0x42: return Opcode::PUSH_ARG_LIST_NO_RET;
        case 0x43: return Opcode::PUSH_ARG_LIST;
        case 0x44: return Opcode::PUSH_CONS;
        case 0x45: return Opcode::PUSH_SYMB;
        case 0x46: return Opcode::PUSH_VAR_REF;
        case 0x48: return Opcode::GET_GLOBAL2;
        case 0x49: return Opcode::GET_GLOBAL;
        case 0x4A: return Opcode::GET_PROP;
        case 0x4B: return Opcode::GET_PARAM;
        case 0x4C: return Opcode::GET_LOCAL;
        case 0x4E: return Opcode::SET_GLOBAL2;
        case 0x4F: return Opcode::SET_GLOBAL;
        case 0x50: return Opcode::SET_PROP;
        case 0x51: return Opcode::SET_PARAM;
        case 0x52: return Opcode::SET_LOCAL;
        case 0x53: return Opcode::JMP;
        case 0x54: return Opcode::END_REPEAT;
        case 0x55: return Opcode::JMP_IF_Z;
        case 0x56: return Opcode::LOCAL_CALL;
        case 0x57: return Opcode::EXT_CALL;
        case 0x58: return Opcode::OBJ_CALL_V4;
        case 0x59: return Opcode::PUT;
        case 0x5A: return Opcode::PUT_CHUNK;
        case 0x5B: return Opcode::DELETE_CHUNK;
        case 0x5C: return Opcode::GET;
        case 0x5D: return Opcode::SET;
        case 0x5F: return Opcode::GET_MOVIE_PROP;
        case 0x60: return Opcode::SET_MOVIE_PROP;
        case 0x61: return Opcode::GET_OBJ_PROP;
        case 0x62: return Opcode::SET_OBJ_PROP;
        case 0x63: return Opcode::TELL_CALL;
        case 0x64: return Opcode::PEEK;
        case 0x65: return Opcode::POP;
        case 0x66: return Opcode::THE_BUILTIN;
        case 0x67: return Opcode::OBJ_CALL;
        case 0x6D: return Opcode::PUSH_CHUNK_VAR_REF;
        case 0x6E: return Opcode::PUSH_INT16;
        case 0x6F: return Opcode::PUSH_INT32;
        case 0x70: return Opcode::GET_CHAINED_PROP;
        case 0x71: return Opcode::PUSH_FLOAT32;
        case 0x72: return Opcode::GET_TOP_LEVEL_PROP;
        case 0x73: return Opcode::NEW_OBJ;
        default: return Opcode::INVALID;
    }
}

bool isSingleByte(Opcode opcode) {
    return code(opcode) < 0x40;
}

bool isMultiByte(Opcode opcode) {
    return code(opcode) >= 0x40;
}

bool isJump(Opcode opcode) {
    return opcode == Opcode::JMP || opcode == Opcode::JMP_IF_Z || opcode == Opcode::END_REPEAT;
}

bool isCall(Opcode opcode) {
    return opcode == Opcode::LOCAL_CALL ||
           opcode == Opcode::EXT_CALL ||
           opcode == Opcode::OBJ_CALL ||
           opcode == Opcode::OBJ_CALL_V4 ||
           opcode == Opcode::TELL_CALL;
}

bool isReturn(Opcode opcode) {
    return opcode == Opcode::RET || opcode == Opcode::RET_FACTORY;
}

bool isPush(Opcode opcode) {
    switch (opcode) {
        case Opcode::PUSH_ZERO:
        case Opcode::PUSH_INT8:
        case Opcode::PUSH_INT16:
        case Opcode::PUSH_INT32:
        case Opcode::PUSH_FLOAT32:
        case Opcode::PUSH_CONS:
        case Opcode::PUSH_SYMB:
        case Opcode::PUSH_VAR_REF:
        case Opcode::PUSH_CHUNK_VAR_REF:
        case Opcode::PUSH_LIST:
        case Opcode::PUSH_PROP_LIST:
        case Opcode::PUSH_ARG_LIST:
        case Opcode::PUSH_ARG_LIST_NO_RET:
            return true;
        default:
            return false;
    }
}

} // namespace libreshockwave::lingo
