package com.libreshockwave.lingo.decompiler;

import com.libreshockwave.lingo.Opcode;

import java.util.Map;

/**
 * Static property name maps for Lingo decompilation.
 * Ported from ProjectorRays lingo.cpp.
 */
public final class LingoProperties {

    private LingoProperties() {}

    public static final Map<Opcode, String> BINARY_OP_NAMES = Map.ofEntries(
        Map.entry(Opcode.MUL, "*"),
        Map.entry(Opcode.ADD, "+"),
        Map.entry(Opcode.SUB, "-"),
        Map.entry(Opcode.DIV, "/"),
        Map.entry(Opcode.MOD, "mod"),
        Map.entry(Opcode.JOIN_STR, "&"),
        Map.entry(Opcode.JOIN_PAD_STR, "&&"),
        Map.entry(Opcode.LT, "<"),
        Map.entry(Opcode.LT_EQ, "<="),
        Map.entry(Opcode.NT_EQ, "<>"),
        Map.entry(Opcode.EQ, "="),
        Map.entry(Opcode.GT, ">"),
        Map.entry(Opcode.GT_EQ, ">="),
        Map.entry(Opcode.AND, "and"),
        Map.entry(Opcode.OR, "or"),
        Map.entry(Opcode.CONTAINS_STR, "contains"),
        Map.entry(Opcode.CONTAINS_0_STR, "starts")
    );

    public static final Map<Integer, String> CHUNK_TYPE_NAMES = Map.of(
        1, "char",
        2, "word",
        3, "item",
        4, "line"
    );

    public static final Map<Integer, String> PUT_TYPE_NAMES = Map.of(
        1, "into",
        2, "after",
        3, "before"
    );

    public static final Map<Integer, String> MOVIE_PROPERTY_NAMES = Map.ofEntries(
        Map.entry(0x00, "floatPrecision"),
        Map.entry(0x01, "mouseDownScript"),
        Map.entry(0x02, "mouseUpScript"),
        Map.entry(0x03, "keyDownScript"),
        Map.entry(0x04, "keyUpScript"),
        Map.entry(0x05, "timeoutScript"),
        Map.entry(0x06, "short time"),
        Map.entry(0x07, "abbr time"),
        Map.entry(0x08, "long time"),
        Map.entry(0x09, "short date"),
        Map.entry(0x0a, "abbr date"),
        Map.entry(0x0b, "long date")
    );

    public static final Map<Integer, String> WHEN_EVENT_NAMES = Map.of(
        0x01, "mouseDown",
        0x02, "mouseUp",
        0x03, "keyDown",
        0x04, "keyUp",
        0x05, "timeOut"
    );

    public static final Map<Integer, String> MENU_PROPERTY_NAMES = Map.of(
        0x01, "name",
        0x02, "number of menuItems"
    );

    public static final Map<Integer, String> MENU_ITEM_PROPERTY_NAMES = Map.of(
        0x01, "name",
        0x02, "checkMark",
        0x03, "enabled",
        0x04, "script"
    );

    public static final Map<Integer, String> SOUND_PROPERTY_NAMES = Map.of(
        0x01, "volume"
    );

    public static final Map<Integer, String> SPRITE_PROPERTY_NAMES = Map.ofEntries(
        Map.entry(0x01, "type"), Map.entry(0x02, "backColor"), Map.entry(0x03, "bottom"),
        Map.entry(0x04, "castNum"), Map.entry(0x05, "constraint"), Map.entry(0x06, "cursor"),
        Map.entry(0x07, "foreColor"), Map.entry(0x08, "height"), Map.entry(0x09, "immediate"),
        Map.entry(0x0a, "ink"), Map.entry(0x0b, "left"), Map.entry(0x0c, "lineSize"),
        Map.entry(0x0d, "locH"), Map.entry(0x0e, "locV"), Map.entry(0x0f, "movieRate"),
        Map.entry(0x10, "movieTime"), Map.entry(0x11, "pattern"), Map.entry(0x12, "puppet"),
        Map.entry(0x13, "right"), Map.entry(0x14, "startTime"), Map.entry(0x15, "stopTime"),
        Map.entry(0x16, "stretch"), Map.entry(0x17, "top"), Map.entry(0x18, "trails"),
        Map.entry(0x19, "visible"), Map.entry(0x1a, "volume"), Map.entry(0x1b, "width"),
        Map.entry(0x1c, "blend"), Map.entry(0x1d, "scriptNum"), Map.entry(0x1e, "moveableSprite"),
        Map.entry(0x1f, "editableText"), Map.entry(0x20, "scoreColor"), Map.entry(0x21, "loc"),
        Map.entry(0x22, "rect"), Map.entry(0x23, "memberNum"), Map.entry(0x24, "castLibNum"),
        Map.entry(0x25, "member"), Map.entry(0x26, "scriptInstanceList"),
        Map.entry(0x27, "currentTime"), Map.entry(0x28, "mostRecentCuePoint"),
        Map.entry(0x29, "tweened"), Map.entry(0x2a, "name")
    );

    public static final Map<Integer, String> ANIMATION_PROPERTY_NAMES = Map.ofEntries(
        Map.entry(0x01, "beepOn"), Map.entry(0x02, "buttonStyle"), Map.entry(0x03, "centerStage"),
        Map.entry(0x04, "checkBoxAccess"), Map.entry(0x05, "checkboxType"),
        Map.entry(0x06, "colorDepth"), Map.entry(0x07, "colorQD"), Map.entry(0x08, "exitLock"),
        Map.entry(0x09, "fixStageSize"), Map.entry(0x0a, "fullColorPermit"),
        Map.entry(0x0b, "imageDirect"), Map.entry(0x0c, "doubleClick"), Map.entry(0x0d, "key"),
        Map.entry(0x0e, "lastClick"), Map.entry(0x0f, "lastEvent"), Map.entry(0x10, "keyCode"),
        Map.entry(0x11, "lastKey"), Map.entry(0x12, "lastRoll"), Map.entry(0x13, "timeoutLapsed"),
        Map.entry(0x14, "multiSound"), Map.entry(0x15, "pauseState"),
        Map.entry(0x16, "quickTimePresent"), Map.entry(0x17, "selEnd"),
        Map.entry(0x18, "selStart"), Map.entry(0x19, "soundEnabled"),
        Map.entry(0x1a, "soundLevel"), Map.entry(0x1b, "stageColor"),
        Map.entry(0x1d, "switchColorDepth"), Map.entry(0x1e, "timeoutKeyDown"),
        Map.entry(0x1f, "timeoutLength"), Map.entry(0x20, "timeoutMouse"),
        Map.entry(0x21, "timeoutPlay"), Map.entry(0x22, "timer"),
        Map.entry(0x23, "preLoadRAM"), Map.entry(0x24, "videoForWindowsPresent"),
        Map.entry(0x25, "netPresent"), Map.entry(0x26, "safePlayer"),
        Map.entry(0x27, "soundKeepDevice"), Map.entry(0x28, "soundMixMedia")
    );

    public static final Map<Integer, String> ANIMATION2_PROPERTY_NAMES = Map.of(
        0x01, "perFrameHook",
        0x02, "number of castMembers",
        0x03, "number of menus",
        0x04, "number of castLibs",
        0x05, "number of xtras"
    );

    public static final Map<Integer, String> MEMBER_PROPERTY_NAMES = Map.ofEntries(
        Map.entry(0x01, "name"), Map.entry(0x02, "text"), Map.entry(0x03, "textStyle"),
        Map.entry(0x04, "textFont"), Map.entry(0x05, "textHeight"), Map.entry(0x06, "textAlign"),
        Map.entry(0x07, "textSize"), Map.entry(0x08, "picture"), Map.entry(0x09, "hilite"),
        Map.entry(0x0a, "number"), Map.entry(0x0b, "size"), Map.entry(0x0c, "loop"),
        Map.entry(0x0d, "duration"), Map.entry(0x0e, "controller"),
        Map.entry(0x0f, "directToStage"), Map.entry(0x10, "sound"),
        Map.entry(0x11, "foreColor"), Map.entry(0x12, "backColor"), Map.entry(0x13, "type")
    );

    public static String getName(Map<Integer, String> map, int id) {
        return map.getOrDefault(id, "ERROR");
    }
}
