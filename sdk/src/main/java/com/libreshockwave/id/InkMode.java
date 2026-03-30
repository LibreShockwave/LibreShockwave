package com.libreshockwave.id;

import java.util.Locale;

/**
 * Director ink modes for sprite blending/compositing.
 */
public enum InkMode {
    COPY(0),
    TRANSPARENT(1),
    REVERSE(2),
    GHOST(3),
    NOT_COPY(4),
    NOT_TRANSPARENT(5),
    NOT_REVERSE(6),
    NOT_GHOST(7),
    MATTE(8),
    MASK(9),
    BLEND(32),
    ADD_PIN(33),
    ADD(34),
    SUBTRACT_PIN(35),
    BACKGROUND_TRANSPARENT(36),
    LIGHTEST(37),
    SUBTRACT(38),
    DARKEST(39),
    LIGHTEN(40),
    DARKEN(41);

    private final int code;

    InkMode(int code) {
        this.code = code;
    }

    public int code() {
        return code;
    }

    public boolean usesBlend() {
        return this == BLEND || this == ADD_PIN || this == ADD ||
               this == SUBTRACT_PIN || this == SUBTRACT ||
               this == LIGHTEST || this == DARKEST ||
               this == LIGHTEN || this == DARKEN;
    }

    public static InkMode fromCode(int code) {
        for (InkMode mode : values()) {
            if (mode.code == code) return mode;
        }
        return COPY;
    }

    public static InkMode fromNameOrNull(String name) {
        if (name == null || name.isEmpty()) {
            return null;
        }

        String normalized = name
                .replace("_", "")
                .replace("-", "")
                .replace(" ", "")
                .toLowerCase(Locale.ROOT);

        return switch (normalized) {
            case "copy" -> COPY;
            case "transparent" -> TRANSPARENT;
            case "reverse" -> REVERSE;
            case "ghost" -> GHOST;
            case "notcopy" -> NOT_COPY;
            case "nottransparent" -> NOT_TRANSPARENT;
            case "notreverse" -> NOT_REVERSE;
            case "notghost" -> NOT_GHOST;
            case "matte" -> MATTE;
            case "mask" -> MASK;
            case "blend" -> BLEND;
            case "addpin" -> ADD_PIN;
            case "add" -> ADD;
            case "subtractpin" -> SUBTRACT_PIN;
            case "backgroundtransparent", "bgtransparent" -> BACKGROUND_TRANSPARENT;
            case "lightest" -> LIGHTEST;
            case "subtract" -> SUBTRACT;
            case "darkest" -> DARKEST;
            case "lighten" -> LIGHTEN;
            case "darken" -> DARKEN;
            default -> null;
        };
    }
}
