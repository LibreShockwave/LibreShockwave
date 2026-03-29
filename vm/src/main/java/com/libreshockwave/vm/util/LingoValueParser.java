package com.libreshockwave.vm.util;

import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Shared parser for Director/Lingo string values interpreted through value(...).
 */
public final class LingoValueParser {

    private LingoValueParser() {}

    public static Datum parseWithPartial(String expr, LingoVM vm) {
        if (expr == null) {
            return Datum.VOID;
        }
        expr = expr.trim();
        if (expr.isEmpty()) {
            return Datum.VOID;
        }

        Datum completeResult = tryParseComplete(expr, vm);
        if (completeResult != null) {
            return completeResult;
        }
        return parseFirstValidExpression(expr, vm);
    }

    private static Datum tryParseComplete(String expr, LingoVM vm) {
        if (isIntegerLiteral(expr)) {
            try {
                return Datum.of(Integer.parseInt(expr));
            } catch (NumberFormatException ignored) {}
        }

        if (isFloatLiteral(expr)) {
            try {
                return Datum.of(Double.parseDouble(expr));
            } catch (NumberFormatException ignored) {}
        }

        if (expr.startsWith("#") && expr.length() > 1) {
            String symName = expr.substring(1);
            if (isIdentifier(symName)) {
                return Datum.symbol(symName);
            }
        }

        if (expr.startsWith("\"") && expr.endsWith("\"") && expr.length() >= 2) {
            return Datum.of(unescapeString(expr.substring(1, expr.length() - 1)));
        }

        if (expr.startsWith("color(") && expr.endsWith(")")) {
            String inner = expr.substring(6, expr.length() - 1).trim();
            String[] parts = inner.split(",");
            if (parts.length == 3) {
                try {
                    int r = Integer.parseInt(parts[0].trim());
                    int g = Integer.parseInt(parts[1].trim());
                    int b = Integer.parseInt(parts[2].trim());
                    return new Datum.Color(r, g, b);
                } catch (NumberFormatException ignored) {}
            }
        }

        if (expr.startsWith("rgb(") && expr.endsWith(")")) {
            String inner = expr.substring(4, expr.length() - 1).trim();
            if (inner.startsWith("\"") && inner.endsWith("\"")) {
                String hex = inner.substring(1, inner.length() - 1).trim();
                if (hex.startsWith("#")) hex = hex.substring(1);
                try {
                    int colorVal = Integer.parseInt(hex, 16);
                    return new Datum.Color((colorVal >> 16) & 0xFF, (colorVal >> 8) & 0xFF, colorVal & 0xFF);
                } catch (NumberFormatException ignored) {}
            }
            String[] parts = inner.split(",");
            if (parts.length == 3) {
                try {
                    int r = Integer.parseInt(parts[0].trim());
                    int g = Integer.parseInt(parts[1].trim());
                    int b = Integer.parseInt(parts[2].trim());
                    return new Datum.Color(r, g, b);
                } catch (NumberFormatException ignored) {}
            }
            if (parts.length == 1) {
                try {
                    int val = Integer.parseInt(parts[0].trim());
                    return new Datum.Color((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
                } catch (NumberFormatException ignored) {}
            }
        }

        if (expr.startsWith("rect(") && expr.endsWith(")")) {
            String inner = expr.substring(5, expr.length() - 1).trim();
            String[] parts = inner.split(",");
            if (parts.length == 4) {
                try {
                    return new Datum.Rect(
                            Integer.parseInt(parts[0].trim()),
                            Integer.parseInt(parts[1].trim()),
                            Integer.parseInt(parts[2].trim()),
                            Integer.parseInt(parts[3].trim()));
                } catch (NumberFormatException ignored) {}
            }
        }

        if (expr.startsWith("point(") && expr.endsWith(")")) {
            String inner = expr.substring(6, expr.length() - 1).trim();
            String[] parts = inner.split(",");
            if (parts.length == 2) {
                try {
                    return new Datum.Point(
                            Integer.parseInt(parts[0].trim()),
                            Integer.parseInt(parts[1].trim()));
                } catch (NumberFormatException ignored) {}
            }
        }

        if (expr.startsWith("[") && expr.endsWith("]")) {
            try {
                Datum fastFlatList = tryParseFlatLiteralList(expr);
                if (fastFlatList != null) {
                    return fastFlatList;
                }
                return parseListOrPropList(expr.substring(1, expr.length() - 1).trim(), vm);
            } catch (Exception ignored) {}
        }

        if (isIdentifier(expr)) {
            if (expr.equalsIgnoreCase("TRUE")) return Datum.of(1);
            if (expr.equalsIgnoreCase("FALSE")) return Datum.of(0);
            if (expr.equalsIgnoreCase("VOID")) return Datum.VOID;
            if (expr.equalsIgnoreCase("EMPTY")) return Datum.of("");

            Datum globalValue = vm.getGlobal(expr);
            if (!globalValue.isVoid()) {
                return globalValue;
            }
            HandlerRef ref = vm.findHandler(expr);
            if (ref != null) {
                return vm.executeHandler(ref.script(), ref.handler(), List.of(), null);
            }
            return Datum.VOID;
        }

        return null;
    }

    private static Datum parseFirstValidExpression(String expr, LingoVM vm) {
        int pos = 0;
        int len = expr.length();
        while (pos < len && Character.isWhitespace(expr.charAt(pos))) {
            pos++;
        }
        if (pos >= len) {
            return Datum.VOID;
        }

        char first = expr.charAt(pos);
        if (Character.isDigit(first) || (first == '-' && pos + 1 < len && Character.isDigit(expr.charAt(pos + 1)))) {
            int start = pos;
            if (first == '-') pos++;
            while (pos < len && Character.isDigit(expr.charAt(pos))) {
                pos++;
            }
            if (pos < len && expr.charAt(pos) == '.' && pos + 1 < len && Character.isDigit(expr.charAt(pos + 1))) {
                pos++;
                while (pos < len && Character.isDigit(expr.charAt(pos))) {
                    pos++;
                }
                try {
                    return Datum.of(Double.parseDouble(expr.substring(start, pos)));
                } catch (NumberFormatException e) {
                    return Datum.VOID;
                }
            }
            try {
                return Datum.of(Integer.parseInt(expr.substring(start, pos)));
            } catch (NumberFormatException e) {
                return Datum.VOID;
            }
        }

        if (first == '"') {
            int start = pos;
            pos++;
            StringBuilder sb = new StringBuilder();
            while (pos < len && expr.charAt(pos) != '"') {
                if (expr.charAt(pos) == '\\' && pos + 1 < len) {
                    pos++;
                }
                sb.append(expr.charAt(pos));
                pos++;
            }
            if (pos < len && expr.charAt(pos) == '"') {
                return Datum.of(sb.toString());
            }
            return Datum.VOID;
        }

        if (first == '#') {
            pos++;
            int start = pos;
            while (pos < len && (Character.isLetterOrDigit(expr.charAt(pos)) || expr.charAt(pos) == '_')) {
                pos++;
            }
            if (pos > start) {
                return Datum.symbol(expr.substring(start, pos));
            }
            return Datum.VOID;
        }

        if (first == '[') {
            int bracketDepth = 1;
            int start = pos;
            pos++;
            while (pos < len && bracketDepth > 0) {
                char c = expr.charAt(pos);
                if (c == '[') bracketDepth++;
                else if (c == ']') bracketDepth--;
                else if (c == '"') {
                    pos++;
                    while (pos < len && expr.charAt(pos) != '"') {
                        if (expr.charAt(pos) == '\\' && pos + 1 < len) pos++;
                        pos++;
                    }
                }
                pos++;
            }
            if (bracketDepth == 0) {
                String listExpr = expr.substring(start, pos);
                try {
                    return parseListOrPropList(listExpr.substring(1, listExpr.length() - 1).trim(), vm);
                } catch (Exception ignored) {
                    return Datum.VOID;
                }
            }
            return Datum.VOID;
        }

        if (Character.isLetter(first) || first == '_') {
            int start = pos;
            while (pos < len && (Character.isLetterOrDigit(expr.charAt(pos)) || expr.charAt(pos) == '_')) {
                pos++;
            }
            String identifier = expr.substring(start, pos);
            if (identifier.equalsIgnoreCase("TRUE")) return Datum.of(1);
            if (identifier.equalsIgnoreCase("FALSE")) return Datum.of(0);
            if (identifier.equalsIgnoreCase("VOID")) return Datum.VOID;
            if (identifier.equalsIgnoreCase("EMPTY")) return Datum.of("");

            Datum globalValue = vm.getGlobal(identifier);
            if (!globalValue.isVoid()) {
                return globalValue;
            }
            HandlerRef ref = vm.findHandler(identifier);
            if (ref != null) {
                return vm.executeHandler(ref.script(), ref.handler(), List.of(), null);
            }
            return Datum.VOID;
        }

        return Datum.VOID;
    }

    private static String unescapeString(String s) {
        if (!s.contains("\\")) {
            return s;
        }
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\' && i + 1 < s.length()) {
                char next = s.charAt(++i);
                switch (next) {
                    case 'n' -> sb.append('\n');
                    case 'r' -> sb.append('\r');
                    case 't' -> sb.append('\t');
                    case '"' -> sb.append('"');
                    case '\\' -> sb.append('\\');
                    default -> sb.append(next);
                }
            } else {
                sb.append(c);
            }
        }
        return sb.toString();
    }

    private static Datum parseListOrPropList(String content, LingoVM vm) {
        if (content.isEmpty()) {
            return Datum.list();
        }
        if (content.trim().equals(":")) {
            return Datum.propList(new LinkedHashMap<>());
        }

        List<String> elements = splitListElements(content);
        if (elements.isEmpty()) {
            return Datum.list();
        }

        String first = elements.get(0).trim();
        boolean isPropList = isPropListElement(first);
        if (isPropList) {
            Map<String, Datum> props = new LinkedHashMap<>();
            for (String element : elements) {
                element = element.trim();
                int colonIdx = findPropListColon(element);
                if (colonIdx > 0) {
                    String rawKey = element.substring(0, colonIdx).trim();
                    String key;
                    if (rawKey.startsWith("#")) {
                        key = rawKey.substring(1).trim();
                    } else if (rawKey.startsWith("\"") && rawKey.endsWith("\"") && rawKey.length() >= 2) {
                        key = rawKey.substring(1, rawKey.length() - 1);
                    } else {
                        key = rawKey;
                    }
                    String valueStr = element.substring(colonIdx + 1).trim();
                    props.put(key, parseWithPartial(valueStr, vm));
                }
            }
            return Datum.propList(props);
        }

        List<Datum> items = new ArrayList<>();
        for (String element : elements) {
            items.add(parseWithPartial(element.trim(), vm));
        }
        return Datum.list(items);
    }

    private static List<String> splitListElements(String content) {
        List<String> elements = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        int bracketDepth = 0;
        int parenDepth = 0;
        boolean inQuote = false;

        for (int i = 0; i < content.length(); i++) {
            char c = content.charAt(i);
            if (c == '"' && (i == 0 || content.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
                current.append(c);
            } else if (inQuote) {
                current.append(c);
            } else if (c == '[') {
                bracketDepth++;
                current.append(c);
            } else if (c == ']') {
                bracketDepth--;
                current.append(c);
            } else if (c == '(') {
                parenDepth++;
                current.append(c);
            } else if (c == ')') {
                parenDepth--;
                current.append(c);
            } else if (c == ',' && bracketDepth == 0 && parenDepth == 0) {
                elements.add(current.toString());
                current = new StringBuilder();
            } else {
                current.append(c);
            }
        }

        if (current.length() > 0) {
            elements.add(current.toString());
        }
        return elements;
    }

    private static boolean isPropListElement(String element) {
        int colonIdx = findPropListColon(element);
        if (colonIdx < 0) {
            return false;
        }

        String rawKey = element.substring(0, colonIdx).trim();
        if (rawKey.isEmpty()) {
            return true;
        }
        if (rawKey.startsWith("#")) {
            String key = rawKey.substring(1).trim();
            return isIdentifier(key);
        }
        if (rawKey.startsWith("\"") && rawKey.endsWith("\"") && rawKey.length() >= 2) {
            return true;
        }
        return isIdentifier(rawKey);
    }

    private static int findPropListColon(String element) {
        int bracketDepth = 0;
        int parenDepth = 0;
        boolean inQuote = false;

        for (int i = 0; i < element.length(); i++) {
            char c = element.charAt(i);
            if (c == '"' && (i == 0 || element.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
            } else if (!inQuote) {
                if (c == '[') {
                    bracketDepth++;
                } else if (c == ']') {
                    bracketDepth--;
                } else if (c == '(') {
                    parenDepth++;
                } else if (c == ')') {
                    parenDepth--;
                } else if (c == ':' && bracketDepth == 0) {
                    if (parenDepth != 0) {
                        continue;
                    }
                    return i;
                }
            }
        }
        return -1;
    }

    /**
     * Fast path for flat list literals like:
     * ["Broker Manager Class"]
     * ["Manager Template Class","Variable Container Class"]
     * [#foo, 1, "bar"]
     *
     * This avoids the heavier generic parser path for the exact value(...)
     * literals that Fuse/System Props uses during bootstrap and also avoids
     * relying on regex behavior in TeaVM/WASM builds.
     */
    private static Datum tryParseFlatLiteralList(String expr) {
        if (!expr.startsWith("[") || !expr.endsWith("]")) {
            return null;
        }
        String inner = expr.substring(1, expr.length() - 1).trim();
        if (inner.isEmpty()) {
            return Datum.list();
        }
        if (inner.indexOf('[') >= 0 || inner.indexOf(']') >= 0
                || inner.indexOf(':') >= 0 || inner.indexOf('(') >= 0 || inner.indexOf(')') >= 0) {
            return null;
        }

        List<Datum> items = new ArrayList<>();
        int pos = 0;
        while (pos < inner.length()) {
            while (pos < inner.length() && Character.isWhitespace(inner.charAt(pos))) {
                pos++;
            }
            if (pos >= inner.length()) {
                break;
            }

            char c = inner.charAt(pos);
            Datum parsedItem;
            if (c == '"') {
                int start = ++pos;
                StringBuilder sb = new StringBuilder();
                boolean terminated = false;
                while (pos < inner.length()) {
                    char ch = inner.charAt(pos++);
                    if (ch == '\\' && pos < inner.length()) {
                        char escaped = inner.charAt(pos++);
                        switch (escaped) {
                            case 'n' -> sb.append('\n');
                            case 'r' -> sb.append('\r');
                            case 't' -> sb.append('\t');
                            case '"' -> sb.append('"');
                            case '\\' -> sb.append('\\');
                            default -> sb.append(escaped);
                        }
                        continue;
                    }
                    if (ch == '"') {
                        terminated = true;
                        break;
                    }
                    sb.append(ch);
                }
                if (!terminated) {
                    return null;
                }
                parsedItem = Datum.of(sb.toString());
            } else {
                int start = pos;
                while (pos < inner.length() && inner.charAt(pos) != ',') {
                    pos++;
                }
                String token = inner.substring(start, pos).trim();
                if (token.isEmpty()) {
                    return null;
                }
                parsedItem = parseFlatLiteralToken(token);
                if (parsedItem == null) {
                    return null;
                }
            }

            items.add(parsedItem);

            while (pos < inner.length() && Character.isWhitespace(inner.charAt(pos))) {
                pos++;
            }
            if (pos >= inner.length()) {
                break;
            }
            if (inner.charAt(pos) != ',') {
                return null;
            }
            pos++;
        }

        return Datum.list(items);
    }

    private static Datum parseFlatLiteralToken(String token) {
        if (token.equalsIgnoreCase("TRUE")) return Datum.of(1);
        if (token.equalsIgnoreCase("FALSE")) return Datum.of(0);
        if (token.equalsIgnoreCase("VOID")) return Datum.VOID;
        if (token.equalsIgnoreCase("EMPTY")) return Datum.of("");
        if (isIntegerLiteral(token)) {
            try {
                return Datum.of(Integer.parseInt(token));
            } catch (NumberFormatException ignored) {
                return null;
            }
        }
        if (isFloatLiteral(token)) {
            try {
                return Datum.of(Double.parseDouble(token));
            } catch (NumberFormatException ignored) {
                return null;
            }
        }
        if (token.startsWith("#") && token.length() > 1 && isIdentifier(token.substring(1))) {
            return Datum.symbol(token.substring(1));
        }
        return null;
    }

    private static boolean isIdentifier(String value) {
        if (value == null || value.isEmpty()) {
            return false;
        }
        char first = value.charAt(0);
        if (!Character.isLetter(first) && first != '_') {
            return false;
        }
        for (int i = 1; i < value.length(); i++) {
            char c = value.charAt(i);
            if (!Character.isLetterOrDigit(c) && c != '_') {
                return false;
            }
        }
        return true;
    }

    private static boolean isIntegerLiteral(String value) {
        if (value == null || value.isEmpty()) {
            return false;
        }
        int start = value.charAt(0) == '-' ? 1 : 0;
        if (start == value.length()) {
            return false;
        }
        for (int i = start; i < value.length(); i++) {
            if (!Character.isDigit(value.charAt(i))) {
                return false;
            }
        }
        return true;
    }

    private static boolean isFloatLiteral(String value) {
        if (value == null || value.isEmpty()) {
            return false;
        }
        int start = value.charAt(0) == '-' ? 1 : 0;
        if (start == value.length()) {
            return false;
        }
        int dotIndex = -1;
        for (int i = start; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c == '.') {
                if (dotIndex >= 0) {
                    return false;
                }
                dotIndex = i;
            } else if (!Character.isDigit(c)) {
                return false;
            }
        }
        return dotIndex > start && dotIndex < value.length() - 1;
    }
}
