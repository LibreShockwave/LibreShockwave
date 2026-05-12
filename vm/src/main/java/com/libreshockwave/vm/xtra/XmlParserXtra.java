package com.libreshockwave.vm.xtra;

import com.libreshockwave.vm.datum.Datum;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Minimal Director XML Parser Xtra compatibility layer.
 *
 * The original Xtra exposes parsed XML nodes as property-list-like objects
 * with #name, #child, #attributeName and #attributeValue properties. Habbo's
 * figure loaders use only that surface, so this parser keeps the implementation
 * deliberately small and deterministic.
 */
public class XmlParserXtra implements Xtra {

    private final Map<Integer, InstanceState> instances = new HashMap<>();
    private int nextInstanceId = 1;

    @Override
    public String getName() {
        return "xmlparser";
    }

    @Override
    public int createInstance(List<Datum> args) {
        int id = nextInstanceId++;
        instances.put(id, new InstanceState());
        return id;
    }

    @Override
    public void destroyInstance(int instanceId) {
        instances.remove(instanceId);
    }

    @Override
    public Datum callHandler(int instanceId, String handlerName, List<Datum> args) {
        InstanceState state = instances.get(instanceId);
        if (state == null) {
            return Datum.VOID;
        }

        return switch (handlerName.toLowerCase()) {
            case "parsestring" -> parseString(state, args);
            case "geterror" -> state.error == null ? Datum.VOID : Datum.of(state.error);
            case "count" -> count(state.root, args);
            case "getprop", "getpropref", "getaprop", "getproperty" -> getProp(state.root, args);
            default -> Datum.VOID;
        };
    }

    @Override
    public Datum getProperty(int instanceId, String propertyName) {
        InstanceState state = instances.get(instanceId);
        if (state == null || state.root == null) {
            return Datum.VOID;
        }
        return state.root.getOrDefault(propertyName, true, Datum.VOID);
    }

    @Override
    public void setProperty(int instanceId, String propertyName, Datum value) {
    }

    private static Datum parseString(InstanceState state, List<Datum> args) {
        state.root = emptyDocument();
        state.error = null;
        if (args.isEmpty() || args.get(0).isVoid()) {
            state.error = "No XML string supplied";
            return Datum.ZERO;
        }

        try {
            state.root = new Parser(args.get(0).toStr()).parseDocument();
            return Datum.TRUE;
        } catch (RuntimeException ex) {
            state.error = ex.getMessage();
            state.root = emptyDocument();
            return Datum.ZERO;
        }
    }

    private static Datum count(Datum.PropList root, List<Datum> args) {
        if (root == null) {
            return Datum.ZERO;
        }
        if (args.isEmpty()) {
            return Datum.of(root.size());
        }
        Datum value = root.getOrDefault(args.get(0).toKeyName(), args.get(0) instanceof Datum.Symbol, Datum.VOID);
        if (value instanceof Datum.List list) {
            return Datum.of(list.items().size());
        }
        if (value instanceof Datum.PropList propList) {
            return Datum.of(propList.size());
        }
        return Datum.ZERO;
    }

    private static Datum getProp(Datum.PropList root, List<Datum> args) {
        if (root == null || args.isEmpty()) {
            return Datum.VOID;
        }
        Datum key = args.get(0);
        Datum value = root.getOrDefault(key.toKeyName(), key instanceof Datum.Symbol, Datum.VOID);
        if (args.size() >= 2 && value instanceof Datum.List list) {
            int index = args.get(1).toInt() - 1;
            if (index >= 0 && index < list.items().size()) {
                return list.items().get(index);
            }
            return Datum.VOID;
        }
        return value;
    }

    private static Datum.PropList emptyDocument() {
        Datum.PropList root = new Datum.PropList();
        root.add("child", new Datum.List(List.of()), true);
        return root;
    }

    private static Datum.PropList node(String name, List<Datum> children,
                                       List<Datum> attributeNames, List<Datum> attributeValues) {
        Datum.PropList node = new Datum.PropList();
        node.add("name", Datum.of(name), true);
        node.add("child", new Datum.List(children), true);
        node.add("attributeName", new Datum.List(attributeNames), true);
        node.add("attributeValue", new Datum.List(attributeValues), true);
        return node;
    }

    private static Datum.PropList textNode(String text) {
        Datum.PropList node = new Datum.PropList();
        node.add("name", Datum.of("#text"), true);
        node.add("text", Datum.of(text), true);
        node.add("child", new Datum.List(List.of()), true);
        node.add("attributeName", new Datum.List(List.of()), true);
        node.add("attributeValue", new Datum.List(List.of()), true);
        return node;
    }

    private static final class InstanceState {
        private Datum.PropList root = emptyDocument();
        private String error;
    }

    private static final class Parser {
        private final String xml;
        private int pos;

        Parser(String xml) {
            this.xml = xml;
        }

        Datum.PropList parseDocument() {
            java.util.ArrayList<Datum> children = new java.util.ArrayList<>();
            while (true) {
                skipWhitespace();
                if (pos >= xml.length()) {
                    return node("#document", children, List.of(), List.of());
                }
                if (startsWith("<?")) {
                    skipUntil("?>");
                    continue;
                }
                if (startsWith("<!--")) {
                    skipUntil("-->");
                    continue;
                }
                if (startsWith("<!")) {
                    skipDeclaration();
                    continue;
                }
                if (peek() == '<') {
                    children.add(parseElement());
                    continue;
                }
                readIgnoredDocumentText();
            }
        }

        private Datum.PropList parseElement() {
            expect('<');
            if (peek() == '/') {
                throw error("Unexpected closing tag");
            }
            String name = readName();
            java.util.ArrayList<Datum> attrNames = new java.util.ArrayList<>();
            java.util.ArrayList<Datum> attrValues = new java.util.ArrayList<>();

            while (true) {
                skipWhitespace();
                if (startsWith("/>")) {
                    pos += 2;
                    return node(name, List.of(), attrNames, attrValues);
                }
                if (startsWith(">")) {
                    pos++;
                    break;
                }
                String attrName = readName();
                skipWhitespace();
                expect('=');
                skipWhitespace();
                String attrValue = readQuotedValue();
                attrNames.add(Datum.of(attrName));
                attrValues.add(Datum.of(decodeEntities(attrValue)));
            }

            java.util.ArrayList<Datum> children = new java.util.ArrayList<>();
            StringBuilder textContent = new StringBuilder();
            while (true) {
                if (pos >= xml.length()) {
                    throw error("Unclosed tag: " + name);
                }
                if (startsWith("</")) {
                    pos += 2;
                    String closeName = readName();
                    skipWhitespace();
                    expect('>');
                    if (!name.equals(closeName)) {
                        throw error("Mismatched closing tag: " + closeName);
                    }
                    if (children.isEmpty() && textContent.length() > 0) {
                        children.add(textNode(decodeEntities(textContent.toString())));
                    }
                    return node(name, children, attrNames, attrValues);
                }
                if (startsWith("<!--")) {
                    skipUntil("-->");
                    continue;
                }
                if (startsWith("<![CDATA[")) {
                    skipUntil("]]>");
                    continue;
                }
                if (peek() == '<') {
                    children.add(parseElement());
                } else {
                    String text = readText();
                    if (hasNonWhitespace(text)) {
                        textContent.append(text.trim());
                    }
                }
            }
        }

        private String readName() {
            int start = pos;
            while (pos < xml.length()) {
                char c = xml.charAt(pos);
                if (Character.isLetterOrDigit(c) || c == '_' || c == '-' || c == '.' || c == ':') {
                    pos++;
                } else {
                    break;
                }
            }
            if (start == pos) {
                throw error("Expected XML name");
            }
            return xml.substring(start, pos);
        }

        private String readQuotedValue() {
            char quote = peek();
            if (quote != '"' && quote != '\'') {
                throw error("Expected quoted attribute value");
            }
            pos++;
            int start = pos;
            while (pos < xml.length() && xml.charAt(pos) != quote) {
                pos++;
            }
            if (pos >= xml.length()) {
                throw error("Unclosed attribute value");
            }
            String value = xml.substring(start, pos);
            pos++;
            return value;
        }

        private void skipWhitespace() {
            while (pos < xml.length() && Character.isWhitespace(xml.charAt(pos))) {
                pos++;
            }
        }

        private void readIgnoredDocumentText() {
            readText();
        }

        private String readText() {
            int start = pos;
            while (pos < xml.length() && xml.charAt(pos) != '<') {
                pos++;
            }
            return xml.substring(start, pos);
        }

        private void skipDeclaration() {
            while (pos < xml.length() && xml.charAt(pos) != '>') {
                pos++;
            }
            if (pos < xml.length()) {
                pos++;
            }
        }

        private void skipUntil(String token) {
            int end = xml.indexOf(token, pos + token.length());
            if (end < 0) {
                throw error("Unclosed XML section");
            }
            pos = end + token.length();
        }

        private boolean startsWith(String token) {
            return xml.startsWith(token, pos);
        }

        private char peek() {
            if (pos >= xml.length()) {
                throw error("Unexpected end of XML");
            }
            return xml.charAt(pos);
        }

        private void expect(char expected) {
            if (peek() != expected) {
                throw error("Expected '" + expected + "'");
            }
            pos++;
        }

        private RuntimeException error(String message) {
            return new IllegalArgumentException(message + " at offset " + pos);
        }

        private static boolean hasNonWhitespace(String value) {
            for (int i = 0; i < value.length(); i++) {
                if (value.charAt(i) > ' ') {
                    return true;
                }
            }
            return false;
        }

        private static String decodeEntities(String value) {
            return value
                    .replace("&quot;", "\"")
                    .replace("&apos;", "'")
                    .replace("&lt;", "<")
                    .replace("&gt;", ">")
                    .replace("&amp;", "&");
        }
    }
}
