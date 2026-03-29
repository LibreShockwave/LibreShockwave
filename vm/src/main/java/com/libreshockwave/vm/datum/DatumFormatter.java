package com.libreshockwave.vm.datum;

import com.libreshockwave.vm.util.StringUtils;

import java.util.Map;

/**
 * Utility for formatting Datum values as human-readable strings.
 */
public final class DatumFormatter {

    private static final int DEFAULT_MAX_STRING_LENGTH = 50;
    private static final int DEFAULT_BRIEF_STRING_LENGTH = 30;
    private static final String INDENT = "  ";

    private DatumFormatter() {}

    /**
     * Escape special characters for JSON string display.
     * Uses visible tokens like [CR], [LF], [TAB] for control characters.
     */
    private static String escapeForJson(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\r\n", "[CR][LF]")
                .replace("\r", "[CR]")
                .replace("\n", "[LF]")
                .replace("\t", "[TAB]");
    }

    /**
     * Format a Datum value as a string.
     */
    public static String format(Datum d) {
        return format(d, DEFAULT_MAX_STRING_LENGTH);
    }

    /**
     * Format a Datum value as a string with configurable max string length.
     */
    public static String format(Datum d, int maxStringLength) {
        if (d == null) return "<null>";
        if (d instanceof Datum.Void) return "<void>";
        if (d instanceof Datum.Int i) return String.valueOf(i.value());
        if (d instanceof Datum.Float f) return String.valueOf(f.value());
        if (d instanceof Datum.Str s) return "\"" + StringUtils.truncate(s.value(), maxStringLength) + "\"";
        if (d instanceof Datum.FieldText ft) return "\"" + StringUtils.truncate(ft.value(), maxStringLength) + "\"";
        if (d instanceof Datum.Symbol sym) return "#" + sym.name();
        if (d instanceof Datum.List list) return "[list:" + list.items().size() + "]";
        if (d instanceof Datum.PropList pl) return "[propList:" + pl.size() + "]";
        if (d instanceof Datum.ArgList al) return "<arglist:" + al.count() + ">";
        if (d instanceof Datum.ArgListNoRet al) return "<arglist-noret:" + al.count() + ">";
        if (d instanceof Datum.Point p) return "point(" + p.x() + ", " + p.y() + ")";
        if (d instanceof Datum.Rect r) return "rect(" + r.left() + ", " + r.top() + ", " + r.right() + ", " + r.bottom() + ")";
        if (d instanceof Datum.Color c) return "color(" + c.r() + ", " + c.g() + ", " + c.b() + ")";
        if (d instanceof Datum.ScriptInstance si) return "<script#" + si.scriptId() + ">";
        if (d instanceof Datum.SpriteRef sr) return "sprite(" + sr.channelNum() + ")";
        if (d instanceof Datum.CastMemberRef cm) return "member(" + cm.memberNum() + ", " + cm.castLibNum() + ")";
        if (d instanceof Datum.CastLibRef cl) return "castLib(" + cl.castLibNum() + ")";
        if (d instanceof Datum.StageRef) return "(the stage)";
        if (d instanceof Datum.XtraRef xr) return "<Xtra \"" + xr.xtraName() + "\">";
        if (d instanceof Datum.XtraInstance xi) return "<XtraInstance \"" + xi.xtraName() + "\" #" + xi.instanceId() + ">";
        return d.toString();
    }

    /**
     * Format a Datum value with its type prefix.
     */
    public static String formatWithType(Datum d) {
        if (d == null) return "<null>";
        return d.typeName() + ": " + format(d);
    }

    /**
     * Format a Datum briefly for compact display (e.g., in nested contexts).
     * Uses shorter string truncation and simple type indicators.
     */
    public static String formatBrief(Datum d) {
        if (d == null) return "<null>";

        return switch (d) {
            case Datum.Void v -> "<Void>";
            case Datum.Int i -> String.valueOf(i.value());
            case Datum.Float f -> String.valueOf(f.value());
            case Datum.Str s -> "\"" + StringUtils.truncate(StringUtils.escapeForDisplay(s.value()), DEFAULT_BRIEF_STRING_LENGTH) + "\"";
            case Datum.FieldText ft -> "\"" + StringUtils.truncate(StringUtils.escapeForDisplay(ft.value()), DEFAULT_BRIEF_STRING_LENGTH) + "\"";
            case Datum.Symbol sym -> "#" + sym.name();
            case Datum.List list -> "[list:" + list.items().size() + "]";
            case Datum.PropList pl -> "[propList:" + pl.size() + "]";
            case Datum.ArgList al -> "<arglist:" + al.count() + ">";
            case Datum.ArgListNoRet al -> "<arglist-noret:" + al.count() + ">";
            case Datum.ScriptInstance si -> "<script#" + si.scriptId() + ">";
            default -> d.toString();
        };
    }

    /**
     * Format a Datum as pretty-printed JSON.
     *
     * @param d the Datum to format
     * @param indent current indentation level (0 for top-level)
     * @return JSON-formatted string
     */
    public static String formatDetailed(Datum d, int indent) {
        if (d == null) return "null";

        String pad = INDENT.repeat(indent);
        String innerPad = INDENT.repeat(indent + 1);

        return switch (d) {
            case Datum.Void v -> "null";
            case Datum.Int i -> String.valueOf(i.value());
            case Datum.Float f -> String.valueOf(f.value());
            case Datum.Str s -> "\"" + escapeForJson(s.value()) + "\"";
            case Datum.FieldText ft -> "\"" + escapeForJson(ft.value()) + "\"";
            case Datum.Symbol sym -> "\"#" + sym.name() + "\"";

            case Datum.ArgList argList -> {
                if (argList.items().isEmpty()) {
                    yield "[]";
                }
                StringBuilder sb = new StringBuilder();
                sb.append("[\n");
                for (int i = 0; i < argList.items().size(); i++) {
                    sb.append(innerPad).append(formatDetailed(argList.items().get(i), indent + 1));
                    if (i < argList.items().size() - 1) sb.append(",");
                    sb.append("\n");
                }
                sb.append(pad).append("]");
                yield sb.toString();
            }

            case Datum.ArgListNoRet argList -> {
                if (argList.items().isEmpty()) {
                    yield "[]";
                }
                StringBuilder sb = new StringBuilder();
                sb.append("[\n");
                for (int i = 0; i < argList.items().size(); i++) {
                    sb.append(innerPad).append(formatDetailed(argList.items().get(i), indent + 1));
                    if (i < argList.items().size() - 1) sb.append(",");
                    sb.append("\n");
                }
                sb.append(pad).append("]");
                yield sb.toString();
            }

            case Datum.List list -> {
                if (list.items().isEmpty()) {
                    yield "[]";
                }
                StringBuilder sb = new StringBuilder();
                sb.append("[\n");
                for (int i = 0; i < list.items().size(); i++) {
                    sb.append(innerPad).append(formatDetailed(list.items().get(i), indent + 1));
                    if (i < list.items().size() - 1) sb.append(",");
                    sb.append("\n");
                }
                sb.append(pad).append("]");
                yield sb.toString();
            }

            case Datum.PropList propList -> {
                if (propList.isEmpty()) {
                    yield "{}";
                }
                StringBuilder sb = new StringBuilder();
                sb.append("{\n");
                var entries = propList.entries();
                for (int i = 0; i < entries.size(); i++) {
                    Datum.PropEntry entry = entries.get(i);
                    sb.append(innerPad).append("\"#").append(escapeForJson(entry.key())).append("\": ");
                    sb.append(formatDetailed(entry.value(), indent + 1));
                    if (i < entries.size() - 1) sb.append(",");
                    sb.append("\n");
                }
                sb.append(pad).append("}");
                yield sb.toString();
            }

            case Datum.ScriptInstance si -> {
                StringBuilder sb = new StringBuilder();
                sb.append("{\n");
                sb.append(innerPad).append("\"_type\": \"ScriptInstance\",\n");
                sb.append(innerPad).append("\"_scriptId\": ").append(si.scriptId());
                if (!si.properties().isEmpty()) {
                    sb.append(",\n");
                    var entries = si.properties().entrySet().toArray(new Map.Entry[0]);
                    for (int i = 0; i < entries.length; i++) {
                        @SuppressWarnings("unchecked")
                        Map.Entry<String, Datum> entry = entries[i];
                        sb.append(innerPad).append("\"").append(escapeForJson(entry.getKey())).append("\": ");
                        sb.append(formatDetailed(entry.getValue(), indent + 1));
                        if (i < entries.length - 1) sb.append(",");
                        sb.append("\n");
                    }
                } else {
                    sb.append("\n");
                }
                sb.append(pad).append("}");
                yield sb.toString();
            }

            case Datum.Point p -> "{ \"x\": " + p.x() + ", \"y\": " + p.y() + " }";
            case Datum.Rect r -> "{ \"left\": " + r.left() + ", \"top\": " + r.top() + ", \"right\": " + r.right() + ", \"bottom\": " + r.bottom() + " }";
            case Datum.Color c -> "{ \"r\": " + c.r() + ", \"g\": " + c.g() + ", \"b\": " + c.b() + " }";
            case Datum.SpriteRef sr -> "\"sprite(" + sr.channelNum() + ")\"";
            case Datum.CastMemberRef cm -> "\"member(" + cm.memberNum() + ", " + cm.castLibNum() + ")\"";
            case Datum.CastLibRef cl -> "\"castLib(" + cl.castLibNum() + ")\"";
            case Datum.StageRef sr -> "\"(the stage)\"";
            case Datum.XtraRef xr -> "\"xtra(\\\"" + escapeForJson(xr.xtraName()) + "\\\")\"";
            case Datum.XtraInstance xi -> "\"XtraInstance \\\"" + escapeForJson(xi.xtraName()) + "\\\" #" + xi.instanceId() + "\"";
            case Datum.ScriptRef sr -> "\"script(" + sr.memberNum() + ", " + sr.castLibNum() + ")\"";
            default -> "\"" + escapeForJson(d.toString()) + "\"";
        };
    }

    /**
     * Get a simple type name for a Datum suitable for display in tables.
     * Returns the class simple name with nested class notation ($ replaced with .).
     *
     * @param d the Datum (may be null)
     * @return type name string, or "null" if d is null
     */
    public static String getTypeName(Datum d) {
        if (d == null) return "null";
        return d.getClass().getSimpleName().replace("$", ".");
    }
}
