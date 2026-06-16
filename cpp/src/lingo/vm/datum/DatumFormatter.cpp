#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"

#include <sstream>
#include <string_view>
#include <unordered_set>

#include "libreshockwave/util/StringUtils.hpp"

namespace libreshockwave::lingo::vm::datum {
namespace {

constexpr int DEFAULT_MAX_STRING_LENGTH = 50;
constexpr int DEFAULT_BRIEF_STRING_LENGTH = 30;
constexpr std::string_view INDENT = "  ";

std::string repeatIndent(int indent) {
    std::string pad;
    for (int i = 0; i < indent; ++i) {
        pad += INDENT;
    }
    return pad;
}

std::string floatToString(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

std::string scriptInstanceDisplayId(const Datum::ScriptInstanceRef& instance) {
    return !instance.scriptName().empty()
        ? instance.scriptName()
        : std::to_string(instance.identityId());
}

std::string escapeForJson(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\\') {
            escaped += "\\\\";
        } else if (ch == '"') {
            escaped += "\\\"";
        } else if (ch == '\r' && i + 1 < value.size() && value[i + 1] == '\n') {
            escaped += "[CR][LF]";
            ++i;
        } else if (ch == '\r') {
            escaped += "[CR]";
        } else if (ch == '\n') {
            escaped += "[LF]";
        } else if (ch == '\t') {
            escaped += "[TAB]";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

std::string formatListCount(const Datum::List& list) {
    return "[list:" + std::to_string(list.count()) + "]";
}

std::string formatPropListCount(const Datum::PropList& propList) {
    return "[propList:" + std::to_string(propList.count()) + "]";
}

std::string propKeyName(const Datum& key) {
    if (const auto* symbol = key.asSymbol()) {
        return symbol->name;
    }
    if (key.isString()) {
        return key.stringValue();
    }
    return format(key);
}

std::string formatArgListCount(const Datum& value, bool noReturn) {
    const int count = noReturn
        ? static_cast<int>(value.argListNoRetValue().args().size())
        : static_cast<int>(value.argListValue().args().size());
    return std::string(noReturn ? "<arglist-noret:" : "<arglist:") + std::to_string(count) + ">";
}

std::string formatBasic(const Datum& value, int maxStringLength, bool brief);
std::string formatExpandedInternal(const Datum& value, std::unordered_set<const void*>& seen);
std::string formatDetailedInternal(const Datum& value, int indent, std::unordered_set<const void*>& seen);

std::string formatBasic(const Datum& value, int maxStringLength, bool brief) {
    switch (value.type()) {
        case DatumType::Null:
            return "<null>";
        case DatumType::Void:
            return brief ? "<Void>" : "<void>";
        case DatumType::Int:
            return std::to_string(value.asInt()->value);
        case DatumType::Float:
            return floatToString(value.asFloat()->value);
        case DatumType::String:
        case DatumType::StringChunk:
        case DatumType::FieldText: {
            std::string text = value.stringValue();
            if (brief) {
                text = ::libreshockwave::util::escapeForDisplay(text);
                maxStringLength = DEFAULT_BRIEF_STRING_LENGTH;
            }
            return "\"" + ::libreshockwave::util::truncate(text, maxStringLength) + "\"";
        }
        case DatumType::Symbol:
            return "#" + value.asSymbol()->name;
        case DatumType::List:
            return formatListCount(value.listValue());
        case DatumType::PropList:
            return formatPropListCount(value.propListValue());
        case DatumType::ArgList:
            return formatArgListCount(value, false);
        case DatumType::ArgListNoRet:
            return formatArgListCount(value, true);
        case DatumType::IntPoint: {
            const auto* point = value.asIntPoint();
            return "point(" + std::to_string(point->x) + ", " + std::to_string(point->y) + ")";
        }
        case DatumType::IntRect: {
            const auto* rect = value.asIntRect();
            return "rect(" + std::to_string(rect->left) + ", " + std::to_string(rect->top) + ", " +
                   std::to_string(rect->right) + ", " + std::to_string(rect->bottom) + ")";
        }
        case DatumType::ColorRef: {
            const auto* color = value.asColorRef();
            if (color->paletteIndex.has_value()) {
                return "paletteIndex(" + std::to_string(*color->paletteIndex) + ")";
            }
            return "color(" + std::to_string(color->r) + ", " + std::to_string(color->g) + ", " +
                   std::to_string(color->b) + ")";
        }
        case DatumType::ScriptInstanceRef:
            return "<script#" + scriptInstanceDisplayId(value.scriptInstanceValue()) + ">";
        case DatumType::SpriteRef:
            return "sprite(" + std::to_string(value.asSpriteRef()->channel) + ")";
        case DatumType::CastMemberRef:
            return "member(" + std::to_string(value.asCastMemberRef()->memberNum()) + ", " +
                   std::to_string(value.asCastMemberRef()->castLib) + ")";
        case DatumType::CastLibRef:
            return "castLib(" + std::to_string(value.asCastLibRef()->castLib) + ")";
        case DatumType::StageRef:
            return "(the stage)";
        case DatumType::Xtra:
            return "<Xtra \"" + value.asXtra()->name + "\">";
        case DatumType::XtraInstance:
            return "<XtraInstance \"" + value.asXtraInstance()->xtraName + "\" #" +
                   std::to_string(value.asXtraInstance()->instanceId) + ">";
        case DatumType::ScriptRef:
            return "script(" + std::to_string(value.asScriptRef()->memberRef.memberNum()) + ", " +
                   std::to_string(value.asScriptRef()->memberRef.castLib) + ")";
        case DatumType::SoundChannel:
            return "sound(" + std::to_string(value.asSoundChannel()->channel) + ")";
        case DatumType::TimeoutRef:
            return "<timeout \"" + value.asTimeoutRef()->name + "\">";
        case DatumType::Media:
            return "[media:" + std::to_string(value.asMedia()->bytes.size()) + "]";
        default:
            try {
                return value.stringValue();
            } catch (const LingoException&) {
                return "<" + value.typeString() + ">";
            }
    }
}

std::string formatListItemsExpanded(const std::vector<Datum>& items,
                                    std::unordered_set<const void*>& seen) {
    std::string result = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += formatExpandedInternal(items[i], seen);
    }
    result += "]";
    return result;
}

std::string formatListExpanded(const Datum::List& list, std::unordered_set<const void*>& seen) {
    if (!seen.insert(&list).second) {
        return "[<recursive-list>]";
    }
    const auto result = formatListItemsExpanded(list.items(), seen);
    seen.erase(&list);
    return result;
}

std::string formatPropListExpanded(const Datum::PropList& propList, std::unordered_set<const void*>& seen) {
    if (!seen.insert(&propList).second) {
        return "[<recursive-proplist>]";
    }
    std::string result = "[";
    const auto& entries = propList.properties();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        const auto& key = entries[i].first;
        if (const auto* symbol = key.asSymbol()) {
            result += "#" + symbol->name;
        } else if (key.isString()) {
            result += "\"" + ::libreshockwave::util::escapeForDisplay(key.stringValue()) + "\"";
        } else {
            result += formatExpandedInternal(key, seen);
        }
        result += ": " + formatExpandedInternal(entries[i].second, seen);
    }
    result += "]";
    seen.erase(&propList);
    return result;
}

std::string formatScriptInstanceExpanded(const Datum::ScriptInstanceRef& instance,
                                         std::unordered_set<const void*>& seen) {
    if (!seen.insert(&instance).second) {
        return "<script#" + scriptInstanceDisplayId(instance) + " <recursive>>";
    }
    std::string result = "<script#" + scriptInstanceDisplayId(instance) + " {";
    const auto& properties = instance.properties();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += properties[i].first + ": " + formatExpandedInternal(properties[i].second, seen);
    }
    result += "}>";
    seen.erase(&instance);
    return result;
}

std::string formatExpandedInternal(const Datum& value, std::unordered_set<const void*>& seen) {
    switch (value.type()) {
        case DatumType::String:
        case DatumType::StringChunk:
        case DatumType::FieldText:
            return "\"" + ::libreshockwave::util::escapeForDisplay(value.stringValue()) + "\"";
        case DatumType::List:
            return formatListExpanded(value.listValue(), seen);
        case DatumType::PropList:
            return formatPropListExpanded(value.propListValue(), seen);
        case DatumType::ArgList:
            return "<arglist " + formatListItemsExpanded(value.argListValue().args(), seen) + ">";
        case DatumType::ArgListNoRet:
            return "<arglist-noret " + formatListItemsExpanded(value.argListNoRetValue().args(), seen) + ">";
        case DatumType::ScriptInstanceRef:
            return formatScriptInstanceExpanded(value.scriptInstanceValue(), seen);
        case DatumType::Xtra:
            return "<Xtra \"" + ::libreshockwave::util::escapeForDisplay(value.asXtra()->name) + "\">";
        case DatumType::XtraInstance:
            return "<XtraInstance \"" + ::libreshockwave::util::escapeForDisplay(value.asXtraInstance()->xtraName) +
                   "\" #" + std::to_string(value.asXtraInstance()->instanceId) + ">";
        default:
            return formatBasic(value, DEFAULT_MAX_STRING_LENGTH, false);
    }
}

std::string formatDetailedList(const std::vector<Datum>& items,
                               int indent,
                               std::unordered_set<const void*>& seen) {
    if (items.empty()) {
        return "[]";
    }
    const auto pad = repeatIndent(indent);
    const auto innerPad = repeatIndent(indent + 1);
    std::string result = "[\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
        result += innerPad + formatDetailedInternal(items[i], indent + 1, seen);
        if (i + 1 < items.size()) {
            result += ",";
        }
        result += "\n";
    }
    result += pad + "]";
    return result;
}

std::string formatDetailedPropList(const Datum::PropList& propList,
                                   int indent,
                                   std::unordered_set<const void*>& seen) {
    if (!seen.insert(&propList).second) {
        return "\"<recursive-proplist>\"";
    }
    if (propList.properties().empty()) {
        seen.erase(&propList);
        return "{}";
    }
    const auto pad = repeatIndent(indent);
    const auto innerPad = repeatIndent(indent + 1);
    std::string result = "{\n";
    const auto& entries = propList.properties();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        result += innerPad + "\"#" + escapeForJson(propKeyName(entries[i].first)) + "\": " +
                  formatDetailedInternal(entries[i].second, indent + 1, seen);
        if (i + 1 < entries.size()) {
            result += ",";
        }
        result += "\n";
    }
    result += pad + "}";
    seen.erase(&propList);
    return result;
}

std::string formatDetailedScriptInstance(const Datum::ScriptInstanceRef& instance,
                                         int indent,
                                         std::unordered_set<const void*>& seen) {
    if (!seen.insert(&instance).second) {
        return "\"<script#" + escapeForJson(scriptInstanceDisplayId(instance)) + " <recursive>>\"";
    }
    const auto pad = repeatIndent(indent);
    const auto innerPad = repeatIndent(indent + 1);
    std::string result = "{\n";
    result += innerPad + "\"_type\": \"ScriptInstance\",\n";
    result += innerPad + "\"_scriptId\": \"" + escapeForJson(scriptInstanceDisplayId(instance)) + "\"";
    const auto& properties = instance.properties();
    if (!properties.empty()) {
        result += ",\n";
        for (std::size_t i = 0; i < properties.size(); ++i) {
            result += innerPad + "\"" + escapeForJson(properties[i].first) + "\": " +
                      formatDetailedInternal(properties[i].second, indent + 1, seen);
            if (i + 1 < properties.size()) {
                result += ",";
            }
            result += "\n";
        }
    } else {
        result += "\n";
    }
    result += pad + "}";
    seen.erase(&instance);
    return result;
}

std::string formatDetailedInternal(const Datum& value, int indent, std::unordered_set<const void*>& seen) {
    switch (value.type()) {
        case DatumType::Null:
        case DatumType::Void:
            return "null";
        case DatumType::Int:
            return std::to_string(value.asInt()->value);
        case DatumType::Float:
            return floatToString(value.asFloat()->value);
        case DatumType::String:
        case DatumType::StringChunk:
        case DatumType::FieldText:
            return "\"" + escapeForJson(value.stringValue()) + "\"";
        case DatumType::Symbol:
            return "\"#" + escapeForJson(value.asSymbol()->name) + "\"";
        case DatumType::List: {
            const auto& list = value.listValue();
            if (!seen.insert(&list).second) {
                return "\"<recursive-list>\"";
            }
            const auto result = formatDetailedList(list.items(), indent, seen);
            seen.erase(&list);
            return result;
        }
        case DatumType::ArgList:
            return formatDetailedList(value.argListValue().args(), indent, seen);
        case DatumType::ArgListNoRet:
            return formatDetailedList(value.argListNoRetValue().args(), indent, seen);
        case DatumType::PropList:
            return formatDetailedPropList(value.propListValue(), indent, seen);
        case DatumType::ScriptInstanceRef:
            return formatDetailedScriptInstance(value.scriptInstanceValue(), indent, seen);
        case DatumType::IntPoint: {
            const auto* point = value.asIntPoint();
            return "{ \"x\": " + std::to_string(point->x) + ", \"y\": " + std::to_string(point->y) + " }";
        }
        case DatumType::IntRect: {
            const auto* rect = value.asIntRect();
            return "{ \"left\": " + std::to_string(rect->left) + ", \"top\": " + std::to_string(rect->top) +
                   ", \"right\": " + std::to_string(rect->right) + ", \"bottom\": " +
                   std::to_string(rect->bottom) + " }";
        }
        case DatumType::ColorRef: {
            const auto* color = value.asColorRef();
            if (color->paletteIndex.has_value()) {
                return "{ \"paletteIndex\": " + std::to_string(*color->paletteIndex) + " }";
            }
            return "{ \"r\": " + std::to_string(color->r) + ", \"g\": " + std::to_string(color->g) +
                   ", \"b\": " + std::to_string(color->b) + " }";
        }
        default:
            return "\"" + escapeForJson(formatBasic(value, DEFAULT_MAX_STRING_LENGTH, false)) + "\"";
    }
}

} // namespace

std::string format(const Datum& value, int maxStringLength) {
    return formatBasic(value, maxStringLength, false);
}

std::string format(const Datum* value, int maxStringLength) {
    return value == nullptr ? "<null>" : format(*value, maxStringLength);
}

std::string formatWithType(const Datum& value) {
    return value.typeString() + ": " + format(value);
}

std::string formatWithType(const Datum* value) {
    return value == nullptr ? "<null>" : formatWithType(*value);
}

std::string formatBrief(const Datum& value) {
    return formatBasic(value, DEFAULT_BRIEF_STRING_LENGTH, true);
}

std::string formatBrief(const Datum* value) {
    return value == nullptr ? "<null>" : formatBrief(*value);
}

std::string formatExpanded(const Datum& value) {
    std::unordered_set<const void*> seen;
    return formatExpandedInternal(value, seen);
}

std::string formatExpanded(const Datum* value) {
    return value == nullptr ? "<null>" : formatExpanded(*value);
}

std::string formatDetailed(const Datum& value, int indent) {
    std::unordered_set<const void*> seen;
    return formatDetailedInternal(value, indent, seen);
}

std::string formatDetailed(const Datum* value, int indent) {
    return value == nullptr ? "null" : formatDetailed(*value, indent);
}

std::string getTypeName(const Datum& value) {
    switch (value.type()) {
        case DatumType::Null: return "Null";
        case DatumType::Void: return "Void";
        case DatumType::Int: return "Int";
        case DatumType::Float: return "Float";
        case DatumType::String: return "Str";
        case DatumType::FieldText: return "FieldText";
        case DatumType::StringChunk: return "StringChunk";
        case DatumType::Symbol: return "Symbol";
        case DatumType::List: return "List";
        case DatumType::PropList: return "PropList";
        case DatumType::ArgList: return "ArgList";
        case DatumType::ArgListNoRet: return "ArgListNoRet";
        case DatumType::ScriptInstanceRef: return "ScriptInstance";
        default: return std::string(value.typeString());
    }
}

std::string getTypeName(const Datum* value) {
    return value == nullptr ? "null" : getTypeName(*value);
}

} // namespace libreshockwave::lingo::vm::datum
