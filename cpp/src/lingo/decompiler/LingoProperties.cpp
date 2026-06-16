#include "libreshockwave/lingo/decompiler/LingoProperties.hpp"

namespace libreshockwave::lingo::decompiler {

std::string_view binaryOpName(Opcode opcode) {
    switch (opcode) {
        case Opcode::MUL: return "*";
        case Opcode::ADD: return "+";
        case Opcode::SUB: return "-";
        case Opcode::DIV: return "/";
        case Opcode::MOD: return "mod";
        case Opcode::JOIN_STR: return "&";
        case Opcode::JOIN_PAD_STR: return "&&";
        case Opcode::LT: return "<";
        case Opcode::LT_EQ: return "<=";
        case Opcode::NT_EQ: return "<>";
        case Opcode::EQ: return "=";
        case Opcode::GT: return ">";
        case Opcode::GT_EQ: return ">=";
        case Opcode::AND: return "and";
        case Opcode::OR: return "or";
        case Opcode::CONTAINS_STR: return "contains";
        case Opcode::CONTAINS_0_STR: return "starts";
        default: return "?";
    }
}

std::string_view chunkTypeName(int id) {
    switch (id) {
        case 1: return "char";
        case 2: return "word";
        case 3: return "item";
        case 4: return "line";
        default: return "chunk";
    }
}

std::string_view putTypeName(int id) {
    switch (id) {
        case 1: return "into";
        case 2: return "after";
        case 3: return "before";
        default: return "into";
    }
}

std::string_view moviePropertyName(int id) {
    switch (id) {
        case 0x00: return "floatPrecision";
        case 0x01: return "mouseDownScript";
        case 0x02: return "mouseUpScript";
        case 0x03: return "keyDownScript";
        case 0x04: return "keyUpScript";
        case 0x05: return "timeoutScript";
        case 0x06: return "short time";
        case 0x07: return "abbr time";
        case 0x08: return "long time";
        case 0x09: return "short date";
        case 0x0a: return "abbr date";
        case 0x0b: return "long date";
        default: return "ERROR";
    }
}

std::string_view whenEventName(int id) {
    switch (id) {
        case 0x01: return "mouseDown";
        case 0x02: return "mouseUp";
        case 0x03: return "keyDown";
        case 0x04: return "keyUp";
        case 0x05: return "timeOut";
        default: return "ERROR";
    }
}

std::string_view menuPropertyName(int id) {
    switch (id) {
        case 0x01: return "name";
        case 0x02: return "number of menuItems";
        default: return "ERROR";
    }
}

std::string_view menuItemPropertyName(int id) {
    switch (id) {
        case 0x01: return "name";
        case 0x02: return "checkMark";
        case 0x03: return "enabled";
        case 0x04: return "script";
        default: return "ERROR";
    }
}

std::string_view soundPropertyName(int id) {
    switch (id) {
        case 0x01: return "volume";
        default: return "ERROR";
    }
}

std::string_view spritePropertyName(int id) {
    switch (id) {
        case 0x01: return "type";
        case 0x02: return "backColor";
        case 0x03: return "bottom";
        case 0x04: return "castNum";
        case 0x05: return "constraint";
        case 0x06: return "cursor";
        case 0x07: return "foreColor";
        case 0x08: return "height";
        case 0x09: return "immediate";
        case 0x0a: return "ink";
        case 0x0b: return "left";
        case 0x0c: return "lineSize";
        case 0x0d: return "locH";
        case 0x0e: return "locV";
        case 0x0f: return "movieRate";
        case 0x10: return "movieTime";
        case 0x11: return "pattern";
        case 0x12: return "puppet";
        case 0x13: return "right";
        case 0x14: return "startTime";
        case 0x15: return "stopTime";
        case 0x16: return "stretch";
        case 0x17: return "top";
        case 0x18: return "trails";
        case 0x19: return "visible";
        case 0x1a: return "volume";
        case 0x1b: return "width";
        case 0x1c: return "blend";
        case 0x1d: return "scriptNum";
        case 0x1e: return "moveableSprite";
        case 0x1f: return "editableText";
        case 0x20: return "scoreColor";
        case 0x21: return "loc";
        case 0x22: return "rect";
        case 0x23: return "memberNum";
        case 0x24: return "castLibNum";
        case 0x25: return "member";
        case 0x26: return "scriptInstanceList";
        case 0x27: return "currentTime";
        case 0x28: return "mostRecentCuePoint";
        case 0x29: return "tweened";
        case 0x2a: return "name";
        default: return "ERROR";
    }
}

std::string_view animationPropertyName(int id) {
    switch (id) {
        case 0x01: return "beepOn";
        case 0x02: return "buttonStyle";
        case 0x03: return "centerStage";
        case 0x04: return "checkBoxAccess";
        case 0x05: return "checkboxType";
        case 0x06: return "colorDepth";
        case 0x07: return "colorQD";
        case 0x08: return "exitLock";
        case 0x09: return "fixStageSize";
        case 0x0a: return "fullColorPermit";
        case 0x0b: return "imageDirect";
        case 0x0c: return "doubleClick";
        case 0x0d: return "key";
        case 0x0e: return "lastClick";
        case 0x0f: return "lastEvent";
        case 0x10: return "keyCode";
        case 0x11: return "lastKey";
        case 0x12: return "lastRoll";
        case 0x13: return "timeoutLapsed";
        case 0x14: return "multiSound";
        case 0x15: return "pauseState";
        case 0x16: return "quickTimePresent";
        case 0x17: return "selEnd";
        case 0x18: return "selStart";
        case 0x19: return "soundEnabled";
        case 0x1a: return "soundLevel";
        case 0x1b: return "stageColor";
        case 0x1d: return "switchColorDepth";
        case 0x1e: return "timeoutKeyDown";
        case 0x1f: return "timeoutLength";
        case 0x20: return "timeoutMouse";
        case 0x21: return "timeoutPlay";
        case 0x22: return "timer";
        case 0x23: return "preLoadRAM";
        case 0x24: return "videoForWindowsPresent";
        case 0x25: return "netPresent";
        case 0x26: return "safePlayer";
        case 0x27: return "soundKeepDevice";
        case 0x28: return "soundMixMedia";
        default: return "ERROR";
    }
}

std::string_view animation2PropertyName(int id) {
    switch (id) {
        case 0x01: return "perFrameHook";
        case 0x02: return "number of castMembers";
        case 0x03: return "number of menus";
        case 0x04: return "number of castLibs";
        case 0x05: return "number of xtras";
        default: return "ERROR";
    }
}

std::string_view memberPropertyName(int id) {
    switch (id) {
        case 0x01: return "name";
        case 0x02: return "text";
        case 0x03: return "textStyle";
        case 0x04: return "textFont";
        case 0x05: return "textHeight";
        case 0x06: return "textAlign";
        case 0x07: return "textSize";
        case 0x08: return "picture";
        case 0x09: return "hilite";
        case 0x0a: return "number";
        case 0x0b: return "size";
        case 0x0c: return "loop";
        case 0x0d: return "duration";
        case 0x0e: return "controller";
        case 0x0f: return "directToStage";
        case 0x10: return "sound";
        case 0x11: return "foreColor";
        case 0x12: return "backColor";
        case 0x13: return "type";
        default: return "ERROR";
    }
}

} // namespace libreshockwave::lingo::decompiler
