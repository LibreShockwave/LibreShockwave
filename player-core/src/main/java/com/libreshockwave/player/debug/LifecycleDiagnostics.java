package com.libreshockwave.player.debug;

import com.libreshockwave.player.sprite.SpriteState;
import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.datum.Datum;

import java.util.List;
import java.util.Locale;

/**
 * Lightweight runtime lifecycle diagnostics.
 * Enabled via the special trace hook name "lifecycle".
 */
public final class LifecycleDiagnostics {

    private static volatile boolean enabled;

    private LifecycleDiagnostics() {}

    public static void setEnabled(boolean enabled) {
        LifecycleDiagnostics.enabled = enabled;
    }

    public static boolean isEnabled() {
        return enabled;
    }

    public static boolean isInterestingHandler(String handlerName) {
        if (handlerName == null || handlerName.isEmpty()) {
            return false;
        }
        return switch (handlerName.toLowerCase(Locale.ROOT)) {
            case "createthread", "initthread", "closethread",
                 "createobject", "removeobject", "getobject", "objectexists",
                 "showprogram", "hideprogram", "createvisualizer", "removevisualizer",
                 "enterroom", "leaveroom", "changeroom" -> true;
            default -> false;
        };
    }

    public static void logHandlerEnter(TraceListener.HandlerInfo info) {
        if (!enabled || info == null || !isInterestingHandler(info.handlerName())) {
            return;
        }
        log("enter handler=" + safe(info.handlerName())
                + " script=" + safe(info.scriptDisplayName())
                + " receiver=" + describeDatum(info.receiver())
                + " args=" + describeArgs(info.arguments()));
    }

    public static void logHandlerExit(TraceListener.HandlerInfo info, Datum returnValue) {
        if (!enabled || info == null || !isInterestingHandler(info.handlerName())) {
            return;
        }
        log("exit handler=" + safe(info.handlerName())
                + " script=" + safe(info.scriptDisplayName())
                + " result=" + describeDatum(returnValue));
    }

    public static void logExternalCastLoaded(int castLibNumber, String fileName) {
        if (!enabled) {
            return;
        }
        log("externalCastLoaded cast=" + castLibNumber + " file=" + safe(fileName));
    }

    public static void logSpriteRemoved(String reason, SpriteState state) {
        if (!enabled || state == null) {
            return;
        }
        log(reason + " channel=" + state.getChannel()
                + " dynamic=" + state.hasDynamicMember()
                + " puppet=" + state.isPuppet()
                + " cast=" + state.getEffectiveCastLib()
                + " member=" + state.getEffectiveCastMember()
                + " scripts=" + state.getScriptInstanceList().size()
                + " loc=" + state.getLocH() + "," + state.getLocV() + "," + state.getLocZ()
                + " size=" + state.getWidth() + "x" + state.getHeight());
    }

    public static void logSpriteMemberCleared(String reason, SpriteState state, int retiredCastLib, int retiredMemberNum) {
        if (!enabled || state == null) {
            return;
        }
        log(reason + " channel=" + state.getChannel()
                + " retired=" + retiredCastLib + ":" + retiredMemberNum
                + " fallback=" + state.getEffectiveCastLib() + ":" + state.getEffectiveCastMember()
                + " puppet=" + state.isPuppet()
                + " scripts=" + state.getScriptInstanceList().size());
    }

    public static void logSpriteEmptyOverride(String reason, SpriteState state) {
        if (!enabled || state == null) {
            return;
        }
        log(reason + " channel=" + state.getChannel()
                + " puppet=" + state.isPuppet()
                + " scripts=" + state.getScriptInstanceList().size());
    }

    public static void logReleasedEmptyChannel(String reason, SpriteState state) {
        if (!enabled || state == null) {
            return;
        }
        log(reason + " channel=" + state.getChannel()
                + " visible=" + state.isVisible()
                + " size=" + state.getWidth() + "x" + state.getHeight()
                + " scripts=" + state.getScriptInstanceList().size());
    }

    public static void logError(String message, Exception error) {
        if (!enabled) {
            return;
        }
        String details = message != null && !message.isEmpty()
                ? message
                : (error != null ? error.getMessage() : "");
        log("error " + safe(details));
    }

    private static void log(String message) {
        System.out.println("[Lifecycle] " + message);
    }

    private static String describeArgs(List<Datum> args) {
        if (args == null || args.isEmpty()) {
            return "[]";
        }
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < args.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append(describeDatum(args.get(i)));
        }
        return sb.append(']').toString();
    }

    private static String describeDatum(Datum datum) {
        if (datum == null) {
            return "null";
        }
        try {
            return safe(datum.toString());
        } catch (Exception ignored) {
            return "<unprintable>";
        }
    }

    private static String safe(String value) {
        if (value == null || value.isEmpty()) {
            return "\"\"";
        }
        return "\"" + value.replace('\n', ' ').replace('\r', ' ') + "\"";
    }
}
