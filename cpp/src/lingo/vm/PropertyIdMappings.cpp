#include "libreshockwave/lingo/vm/PropertyIdMappings.hpp"

namespace libreshockwave::lingo::vm {

std::optional<std::string_view> PropertyIdMappings::getMoviePropName(int id) {
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
        case 0x0A: return "abbr date";
        case 0x0B: return "long date";
        default: return std::nullopt;
    }
}

std::optional<std::string_view> PropertyIdMappings::getSpritePropName(int id) {
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
        case 0x0A: return "ink";
        case 0x0B: return "left";
        case 0x0C: return "lineSize";
        case 0x0D: return "locH";
        case 0x0E: return "locV";
        case 0x0F: return "movieRate";
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
        case 0x1A: return "volume";
        case 0x1B: return "width";
        case 0x1C: return "blend";
        case 0x1D: return "scriptNum";
        case 0x1E: return "moveableSprite";
        case 0x1F: return "editableText";
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
        case 0x2A: return "name";
        default: return std::nullopt;
    }
}

std::optional<std::string_view> PropertyIdMappings::getAnimPropName(int id) {
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
        case 0x0A: return "fullColorPermit";
        case 0x0B: return "imageDirect";
        case 0x0C: return "doubleClick";
        case 0x0D: return "key";
        case 0x0E: return "lastClick";
        case 0x0F: return "lastEvent";
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
        case 0x1A: return "soundLevel";
        case 0x1B: return "stageColor";
        case 0x1D: return "switchColorDepth";
        case 0x1E: return "timeoutKeyDown";
        case 0x1F: return "timeoutLength";
        case 0x20: return "timeoutMouse";
        case 0x21: return "timeoutPlay";
        case 0x22: return "timer";
        case 0x23: return "preLoadRAM";
        case 0x24: return "videoForWindowsPresent";
        case 0x25: return "netPresent";
        case 0x26: return "safePlayer";
        case 0x27: return "soundKeepDevice";
        case 0x28: return "soundMixMedia";
        default: return std::nullopt;
    }
}

std::optional<std::string_view> PropertyIdMappings::getAnim2PropName(int id) {
    switch (id) {
        case 0x01: return "perFrameHook";
        case 0x02: return "number of castMembers";
        case 0x03: return "number of menus";
        case 0x04: return "number of castLibs";
        case 0x05: return "number of xtras";
        default: return std::nullopt;
    }
}

std::string PropertyIdMappings::getSoundPropName(int id) {
    switch (id) {
        case 0x01: return "volume";
        case 0x02: return "pan";
        case 0x03: return "loopCount";
        case 0x04: return "startTime";
        case 0x05: return "endTime";
        case 0x06: return "loopStartTime";
        case 0x07: return "loopEndTime";
        default: return "unknown_sound_prop_" + std::to_string(id);
    }
}

} // namespace libreshockwave::lingo::vm
