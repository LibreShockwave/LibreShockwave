package com.libreshockwave.vm.parse;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.LingoVM;

import java.util.ArrayList;
import java.util.List;

/**
 * Parses Lingo expression strings into Datum values.
 * Handles: integers, floats, symbols, quoted strings, lists, and proplists.
 */
public final class LingoExpressionParser {

    private LingoExpressionParser() {}

    /**
     * Parse a Lingo expression string into a Datum.
     * @param expr The expression string to parse
     * @param vm The VM for handler lookup (may be null for simple parsing)
     * @return The parsed Datum, or VOID if parsing fails
     */
    public static Datum parse(String expr, LingoVM vm) {
        expr = expr.trim();

        if (expr.isEmpty()) {
            return Datum.VOID;
        }

        // Try to parse as integer
        try {
            return Datum.of(Integer.parseInt(expr));
        } catch (NumberFormatException ignored) {}

        // Try to parse as float
        try {
            return Datum.of(Double.parseDouble(expr));
        } catch (NumberFormatException ignored) {}

        // Try to parse as symbol (#symbol)
        if (expr.startsWith("#") && expr.length() > 1 && !expr.contains(":")) {
            String symName = expr.substring(1);
            if (symName.matches("[a-zA-Z_][a-zA-Z0-9_]*")) {
                return Datum.symbol(symName);
            }
        }

        // Try to parse as quoted string
        if (expr.startsWith("\"") && expr.endsWith("\"") && expr.length() >= 2) {
            return Datum.of(expr.substring(1, expr.length() - 1));
        }

        // Try to parse as list or proplist: [...]
        if (expr.startsWith("[") && expr.endsWith("]")) {
            return parseListOrPropList(expr.substring(1, expr.length() - 1).trim(), vm);
        }

        // Try to evaluate as a handler call (simple case: just a handler name)
        if (vm != null && expr.matches("[a-zA-Z_][a-zA-Z0-9_]*")) {
            // Try to find and call a handler with no arguments
            HandlerRef ref = vm.findHandler(expr);
            if (ref != null) {
                return vm.executeHandler(ref.script(), ref.handler(), List.of(), null);
            }
            // Also check if it's a global variable
            Datum globalValue = vm.getGlobal(expr);
            if (!globalValue.isVoid()) {
                return globalValue;
            }
        }

        // Return VOID for expressions we can't evaluate
        return Datum.VOID;
    }

    /**
     * Parse list or proplist content (without the surrounding brackets).
     * Determines if it's a proplist (has #key: value pairs) or a linear list.
     */
    private static Datum parseListOrPropList(String content, LingoVM vm) {
        if (content.isEmpty()) {
            // Empty brackets -> empty list
            return Datum.list();
        }

        // Split by commas (respecting nested brackets and quoted strings)
        List<String> elements = splitListElements(content);

        if (elements.isEmpty()) {
            return Datum.list();
        }

        // Check if first element looks like a proplist entry (#key: value)
        String first = elements.get(0).trim();
        if (first.startsWith("#") && first.contains(":")) {
            // Parse as proplist
            Datum.PropList props = new Datum.PropList();
            for (String element : elements) {
                element = element.trim();
                int colonIdx = findPropListColon(element);
                if (colonIdx > 0 && element.startsWith("#")) {
                    Datum key = Datum.symbol(element.substring(1, colonIdx).trim());
                    String valueStr = element.substring(colonIdx + 1).trim();
                    Datum value = parse(valueStr, vm);
                    props.add(key, value);
                }
            }
            return props;
        } else {
            // Parse as linear list
            List<Datum> items = new ArrayList<>();
            for (String element : elements) {
                items.add(parse(element.trim(), vm));
            }
            return Datum.list(items);
        }
    }

    /**
     * Split list content by commas, respecting nested brackets and quoted strings.
     */
    private static List<String> splitListElements(String content) {
        List<String> elements = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        int bracketDepth = 0;
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
            } else if (c == ',' && bracketDepth == 0) {
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

    /**
     * Find the colon in a proplist entry (#key: value), respecting nested structures.
     */
    private static int findPropListColon(String element) {
        int bracketDepth = 0;
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
                } else if (c == ':' && bracketDepth == 0) {
                    return i;
                }
            }
        }
        return -1;
    }
}
