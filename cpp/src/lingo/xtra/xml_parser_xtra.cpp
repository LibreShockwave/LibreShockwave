#include "libreshockwave/lingo/xtra/xml_parser_xtra.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace libreshockwave::lingo::xtra {
namespace {

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string decodeEntities(std::string value) {
    const auto replace_all = [](std::string& target, std::string_view from, std::string_view to) {
        std::size_t pos = 0;
        while ((pos = target.find(from, pos)) != std::string::npos) {
            target.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all(value, "&quot;", "\"");
    replace_all(value, "&apos;", "'");
    replace_all(value, "&lt;", "<");
    replace_all(value, "&gt;", ">");
    replace_all(value, "&amp;", "&");
    return value;
}

bool hasNonWhitespace(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch > ' ';
    });
}

std::string trimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

Datum makeXmlEmptyDocument() {
    auto root = Datum::propList();
    root.propListValue().put(Datum::symbol("child"), Datum::list());
    return root;
}

Datum makeXmlNode(const std::string& name,
                  std::vector<Datum> children,
                  std::vector<Datum> attribute_names,
                  std::vector<Datum> attribute_values) {
    auto result = Datum::propList();
    auto& props = result.propListValue();
    props.put(Datum::symbol("name"), Datum::of(name));
    props.put(Datum::symbol("child"), Datum::list(std::move(children)));
    props.put(Datum::symbol("attributeName"), Datum::list(std::move(attribute_names)));
    props.put(Datum::symbol("attributeValue"), Datum::list(std::move(attribute_values)));
    return result;
}

Datum makeXmlTextNode(const std::string& text) {
    auto result = Datum::propList();
    auto& props = result.propListValue();
    props.put(Datum::symbol("name"), Datum::of(std::string("#text")));
    props.put(Datum::symbol("text"), Datum::of(text));
    props.put(Datum::symbol("child"), Datum::list());
    props.put(Datum::symbol("attributeName"), Datum::list());
    props.put(Datum::symbol("attributeValue"), Datum::list());
    return result;
}

class Parser {
public:
    explicit Parser(std::string xml) : xml_(std::move(xml)) {}

    [[nodiscard]] Datum parseDocument() {
        std::vector<Datum> children;
        while (true) {
            skipWhitespace();
            if (pos_ >= xml_.size()) {
                return makeXmlNode("#document", std::move(children), {}, {});
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
                children.push_back(parseElement());
                continue;
            }
            (void)readText();
        }
    }

private:
    [[nodiscard]] Datum parseElement() {
        expect('<');
        if (peek() == '/') {
            throw error("Unexpected closing tag");
        }
        const std::string name = readName();
        std::vector<Datum> attr_names;
        std::vector<Datum> attr_values;

        while (true) {
            skipWhitespace();
            if (startsWith("/>")) {
                pos_ += 2;
                return makeXmlNode(name, {}, std::move(attr_names), std::move(attr_values));
            }
            if (startsWith(">")) {
                ++pos_;
                break;
            }
            attr_names.push_back(Datum::of(readName()));
            skipWhitespace();
            expect('=');
            skipWhitespace();
            attr_values.push_back(Datum::of(decodeEntities(readQuotedValue())));
        }

        std::vector<Datum> children;
        std::string text_content;
        while (true) {
            if (pos_ >= xml_.size()) {
                throw error("Unclosed tag: " + name);
            }
            if (startsWith("</")) {
                pos_ += 2;
                const std::string close_name = readName();
                skipWhitespace();
                expect('>');
                if (name != close_name) {
                    throw error("Mismatched closing tag: " + close_name);
                }
                if (children.empty() && !text_content.empty()) {
                    children.push_back(makeXmlTextNode(decodeEntities(text_content)));
                }
                return makeXmlNode(name, std::move(children), std::move(attr_names), std::move(attr_values));
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
                children.push_back(parseElement());
            } else {
                const std::string text = readText();
                if (hasNonWhitespace(text)) {
                    text_content += trimAscii(text);
                }
            }
        }
    }

    [[nodiscard]] std::string readName() {
        const std::size_t start = pos_;
        while (pos_ < xml_.size()) {
            const auto ch = static_cast<unsigned char>(xml_[pos_]);
            if (std::isalnum(ch) != 0 || xml_[pos_] == '_' || xml_[pos_] == '-' ||
                xml_[pos_] == '.' || xml_[pos_] == ':') {
                ++pos_;
            } else {
                break;
            }
        }
        if (start == pos_) {
            throw error("Expected XML name");
        }
        return xml_.substr(start, pos_ - start);
    }

    [[nodiscard]] std::string readQuotedValue() {
        const char quote = peek();
        if (quote != '"' && quote != '\'') {
            throw error("Expected quoted attribute value");
        }
        ++pos_;
        const std::size_t start = pos_;
        while (pos_ < xml_.size() && xml_[pos_] != quote) {
            ++pos_;
        }
        if (pos_ >= xml_.size()) {
            throw error("Unclosed attribute value");
        }
        const auto value = xml_.substr(start, pos_ - start);
        ++pos_;
        return value;
    }

    void skipWhitespace() {
        while (pos_ < xml_.size() && std::isspace(static_cast<unsigned char>(xml_[pos_])) != 0) {
            ++pos_;
        }
    }

    [[nodiscard]] std::string readText() {
        const std::size_t start = pos_;
        while (pos_ < xml_.size() && xml_[pos_] != '<') {
            ++pos_;
        }
        return xml_.substr(start, pos_ - start);
    }

    void skipDeclaration() {
        while (pos_ < xml_.size() && xml_[pos_] != '>') {
            ++pos_;
        }
        if (pos_ < xml_.size()) {
            ++pos_;
        }
    }

    void skipUntil(std::string_view token) {
        const std::size_t end = xml_.find(token, pos_ + token.size());
        if (end == std::string::npos) {
            throw error("Unclosed XML section");
        }
        pos_ = end + token.size();
    }

    [[nodiscard]] bool startsWith(std::string_view token) const {
        return pos_ + token.size() <= xml_.size() && xml_.compare(pos_, token.size(), token) == 0;
    }

    [[nodiscard]] char peek() const {
        if (pos_ >= xml_.size()) {
            throw error("Unexpected end of XML");
        }
        return xml_[pos_];
    }

    void expect(char expected) {
        if (peek() != expected) {
            throw error(std::string("Expected '") + expected + "'");
        }
        ++pos_;
    }

    [[nodiscard]] std::invalid_argument error(const std::string& message) const {
        return std::invalid_argument(message + " at offset " + std::to_string(pos_));
    }

    std::string xml_;
    std::size_t pos_ = 0;
};

} // namespace

std::string XmlParserXtra::name() const {
    return "xmlparser";
}

int XmlParserXtra::createInstance(const std::vector<Datum>&) {
    const int id = next_instance_id_++;
    instances_.emplace(id, InstanceState{makeXmlEmptyDocument(), {}, false});
    return id;
}

void XmlParserXtra::destroyInstance(int instance_id) {
    instances_.erase(instance_id);
}

Datum XmlParserXtra::callHandler(int instance_id,
                                 std::string_view handler_name,
                                 const std::vector<Datum>& args) {
    auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return Datum::voidValue();
    }

    auto& state = it->second;
    const std::string method = lowerAscii(handler_name);
    if (method == "parsestring") {
        state.root = emptyDocument();
        state.error.clear();
        state.has_error = false;
        if (args.empty() || args[0].isVoid()) {
            state.error = "No XML string supplied";
            state.has_error = true;
            return Datum::FALSE;
        }

        try {
            state.root = Parser(args[0].stringValue()).parseDocument();
            return Datum::TRUE;
        } catch (const std::exception& ex) {
            state.error = ex.what();
            state.has_error = true;
            state.root = emptyDocument();
            return Datum::FALSE;
        }
    }
    if (method == "geterror") {
        return state.has_error ? Datum::of(state.error) : Datum::voidValue();
    }
    if (method == "count") {
        return count(state.root, args);
    }
    if (method == "getprop" || method == "getpropref" || method == "getaprop" || method == "getproperty" ||
        method == "getat") {
        return getProp(state.root, args);
    }
    if (method == "name" || method == "child" || method == "attributename" || method == "attributevalue") {
        std::vector<Datum> property_args;
        property_args.reserve(args.size() + 1);
        property_args.push_back(Datum::symbol(std::string(method)));
        property_args.insert(property_args.end(), args.begin(), args.end());
        return getProp(state.root, property_args);
    }
    return Datum::voidValue();
}

Datum XmlParserXtra::getProperty(int instance_id, std::string_view property_name) const {
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return Datum::voidValue();
    }
    return getRootProperty(it->second.root, property_name);
}

void XmlParserXtra::setProperty(int, std::string_view, const Datum&) {}

Datum XmlParserXtra::emptyDocument() {
    return makeXmlEmptyDocument();
}

Datum XmlParserXtra::count(const Datum& root, const std::vector<Datum>& args) {
    if (!root.isPropList()) {
        return Datum::of(0);
    }
    if (args.empty()) {
        return Datum::of(root.propListValue().count());
    }

    const Datum value = getProp(root, {args[0]});
    if (value.isList()) {
        return Datum::of(value.listValue().count());
    }
    if (value.isPropList()) {
        return Datum::of(value.propListValue().count());
    }
    return Datum::of(0);
}

Datum XmlParserXtra::getProp(const Datum& root, const std::vector<Datum>& args) {
    if (!root.isPropList() || args.empty()) {
        return Datum::voidValue();
    }

    Datum value = Datum::voidValue();
    const auto target = lowerAscii(keyName(args[0]));
    for (const auto& entry : root.propListValue().properties()) {
        if (lowerAscii(keyName(entry.first)) == target) {
            value = entry.second;
            break;
        }
    }

    if (args.size() >= 2 && value.isList()) {
        const int index = args[1].intValue() - 1;
        if (index >= 0 && index < value.listValue().count()) {
            return value.listValue().getAt(index + 1);
        }
        return Datum::voidValue();
    }
    return value;
}

Datum XmlParserXtra::getRootProperty(const Datum& root, std::string_view property_name) {
    if (!root.isPropList()) {
        return Datum::voidValue();
    }
    return getProp(root, {Datum::symbol(std::string(property_name))});
}

std::string XmlParserXtra::keyName(const Datum& datum) {
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    return datum.stringValue();
}

bool XmlParserXtra::keyIsSymbol(const Datum& datum) {
    return datum.asSymbol() != nullptr;
}

} // namespace libreshockwave::lingo::xtra
