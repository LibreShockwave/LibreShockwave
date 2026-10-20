#include "datum_format.hpp"

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::debugger {

QString datum_format::toDisplayString(const lingo::Datum& value) {
    if (value.isVoid()) {
        return QStringLiteral("VOID");
    }
    if (value.isInt()) {
        return QString::number(value.intValue());
    }
    if (value.isFloat()) {
        return QString::number(value.floatValue(), 'f', 6);
    }
    if (value.isString()) {
        return QString::fromStdString(value.stringValue());
    }
    if (value.isList()) {
        const auto& items = value.listValue().items();
        QString result = QStringLiteral("[");
        const int max_items = 50;
        int count = 0;
        for (const auto& item : items) {
            if (count > 0) result += QStringLiteral(", ");
            if (count >= max_items) {
                result += QStringLiteral("... (%1 items)").arg(static_cast<int>(items.size()));
                break;
            }
            result += toDisplayString(item);
            ++count;
        }
        result += QStringLiteral("]");
        return result;
    }
    if (value.isPropList()) {
        const auto& props = value.propListValue().properties();
        QString result = QStringLiteral("[");
        const int max_items = 50;
        int count = 0;
        for (const auto& [key, val] : props) {
            if (count > 0) result += QStringLiteral(", ");
            if (count >= max_items) {
                result += QStringLiteral("... (%1 entries)")
                              .arg(static_cast<int>(props.size()));
                break;
            }
            result += toDisplayString(key);
            result += QStringLiteral(": ");
            result += toDisplayString(val);
            ++count;
        }
        result += QStringLiteral("]");
        return result;
    }
    // Fallback: use the type name
    auto type_str = QString::fromStdString(
        std::string(lingo::typeName(value.type())));
    return type_str;
}

QString datum_format::typeName(const lingo::Datum& value) {
    return QString::fromStdString(
        std::string(lingo::typeName(value.type())));
}

bool datum_format::isExpandable(const lingo::Datum& value) {
    return value.isList() || value.isPropList();
}

} // namespace libreshockwave::debugger
