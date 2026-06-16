#include "libreshockwave/lingo/xtra/XmlParserXtra.hpp"

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
    const auto replaceAll = [](std::string& target, std::string_view from, std::string_view to) {
        std::size_t pos = 0;
        while ((pos = target.find(from, pos)) != std::string::npos) {
            target.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll(value, "&quot;", "\"");
    replaceAll(value, "&apos;", "'");
    replaceAll(value, "&lt;", "<");
    replaceAll(value, "&gt;", ">");
    replaceAll(value, "&amp;", "&");
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
                  std::vector<Datum> attributeNames,
                  std::vector<Datum> attributeValues) {
    auto result = Datum::propList();
    auto& props = result.propListValue();
    props.put(Datum::symbol("name"), Datum::of(name));
    props.put(Datum::symbol("child"), Datum::list(std::move(children)));
    props.put(Datum::symbol("attributeName"), Datum::list(std::move(attributeNames)));
    props.put(Datum::symbol("attributeValue"), Datum::list(std::move(attributeValues)));
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
        std::vector<Datum> attrNames;
        std::vector<Datum> attrValues;

        while (true) {
            skipWhitespace();
            if (startsWith("/>")) {
                pos_ += 2;
                return makeXmlNode(name, {}, std::move(attrNames), std::move(attrValues));
            }
            if (startsWith(">")) {
                ++pos_;
                break;
            }
            attrNames.push_back(Datum::of(readName()));
            skipWhitespace();
            expect('=');
            skipWhitespace();
            attrValues.push_back(Datum::of(decodeEntities(readQuotedValue())));
        }

        std::vector<Datum> children;
        std::string textContent;
        while (true) {
            if (pos_ >= xml_.size()) {
                throw error("Unclosed tag: " + name);
            }
            if (startsWith("</")) {
                pos_ += 2;
                const std::string closeName = readName();
                skipWhitespace();
                expect('>');
                if (name != closeName) {
                    throw error("Mismatched closing tag: " + closeName);
                }
                if (children.empty() && !textContent.empty()) {
                    children.push_back(makeXmlTextNode(decodeEntities(textContent)));
                }
                return makeXmlNode(name, std::move(children), std::move(attrNames), std::move(attrValues));
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
                    textContent += trimAscii(text);
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
    const int id = nextInstanceId_++;
    instances_.emplace(id, InstanceState{makeXmlEmptyDocument(), {}, false});
    return id;
}

void XmlParserXtra::destroyInstance(int instanceId) {
    instances_.erase(instanceId);
}

Datum XmlParserXtra::callHandler(int instanceId,
                                 std::string_view handlerName,
                                 const std::vector<Datum>& args) {
    auto it = instances_.find(instanceId);
    if (it == instances_.end()) {
        return Datum::voidValue();
    }

    auto& state = it->second;
    const std::string method = lowerAscii(handlerName);
    if (method == "parsestring") {
        state.root = emptyDocument();
        state.error.clear();
        state.hasError = false;
        if (args.empty() || args[0].isVoid()) {
            state.error = "No XML string supplied";
            state.hasError = true;
            return Datum::FALSE;
        }

        try {
            state.root = Parser(args[0].stringValue()).parseDocument();
            return Datum::TRUE;
        } catch (const std::exception& ex) {
            state.error = ex.what();
            state.hasError = true;
            state.root = emptyDocument();
            return Datum::FALSE;
        }
    }
    if (method == "geterror") {
        return state.hasError ? Datum::of(state.error) : Datum::voidValue();
    }
    if (method == "count") {
        return count(state.root, args);
    }
    if (method == "getprop" || method == "getpropref" || method == "getaprop" || method == "getproperty" ||
        method == "getat") {
        return getProp(state.root, args);
    }
    if (method == "name" || method == "child" || method == "attributename" || method == "attributevalue") {
        std::vector<Datum> propertyArgs;
        propertyArgs.reserve(args.size() + 1);
        propertyArgs.push_back(Datum::symbol(std::string(method)));
        propertyArgs.insert(propertyArgs.end(), args.begin(), args.end());
        return getProp(state.root, propertyArgs);
    }
    return Datum::voidValue();
}

Datum XmlParserXtra::getProperty(int instanceId, std::string_view propertyName) const {
    const auto it = instances_.find(instanceId);
    if (it == instances_.end()) {
        return Datum::voidValue();
    }
    return getRootProperty(it->second.root, propertyName);
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

Datum XmlParserXtra::getRootProperty(const Datum& root, std::string_view propertyName) {
    if (!root.isPropList()) {
        return Datum::voidValue();
    }
    return getProp(root, {Datum::symbol(std::string(propertyName))});
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
