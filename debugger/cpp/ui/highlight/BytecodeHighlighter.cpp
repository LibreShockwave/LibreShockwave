#include "BytecodeHighlighter.hpp"

#include <QTextCharFormat>

#include "DeclarationHighlighting.hpp"

namespace libreshockwave::debugger {

namespace {

// Identifiers inside the annotation column (handler/property names, e.g.
// the "stopMovie" of "<stopMovie()>").
const QRegularExpression kAnnotationIdentifier("[A-Za-z_]\\w*");

// Display-asset colors (VS Code light/dark theme inspired).
const QColor kLightPush(0x0d, 0x94, 0x88);
const QColor kLightGet(0x05, 0x50, 0xae);
const QColor kLightSet(0x6d, 0x28, 0xd9);
const QColor kLightPut(0xa1, 0x62, 0x07);
const QColor kLightControl(0xb9, 0x1c, 0x1c);
const QColor kLightOffset(0x6a, 0x73, 0x7d);
const QColor kLightComment(0x6a, 0x73, 0x7d);
const QColor kDarkPush(0x4e, 0xc9, 0xb0);
const QColor kDarkGet(0x56, 0x9c, 0xd6);
const QColor kDarkSet(0xc5, 0x86, 0xc0);
const QColor kDarkPut(0xe3, 0xb3, 0x41);
const QColor kDarkControl(0xff, 0x7b, 0x72);
const QColor kDarkOffset(0x8b, 0x94, 0x9e);
const QColor kDarkComment(0x8b, 0x94, 0x9e);

} // namespace

BytecodeHighlighter::BytecodeHighlighter(QTextDocument* document)
    : GutterHighlighter(document),
      // Groups: 1 = "> " prefix, 2 = gutter marker, 3 = offset, 4 = opcode,
      // 5 = argument, 6 = "; annotation".
      lineRegex_(QRegularExpression(
          R"(^(> )?(▶ |● |  )(\d{4,})  ([A-Za-z][A-Za-z0-9]*)\s+(-?\d+)(  ;.*)?)")) {
}

void BytecodeHighlighter::setDeclarationNames(const QSet<QString>& names) {
    if (names == declarationNames_) {
        return;
    }
    declarationNames_ = names;
    rehighlight();
}

std::optional<QColor> BytecodeHighlighter::categoryColor(const QString& opcode) {
    const QString lower = opcode.toLower();
    if (darkBase()) {
        if (lower.startsWith(QLatin1String("push"))) return kDarkPush;
        if (lower.startsWith(QLatin1String("get"))) return kDarkGet;
        if (lower.startsWith(QLatin1String("set"))) return kDarkSet;
        if (lower.startsWith(QLatin1String("put"))) return kDarkPut;
        if (lower.startsWith(QLatin1String("jmp")) ||
            lower.startsWith(QLatin1String("call")) ||
            lower.endsWith(QLatin1String("call")) ||
            lower.startsWith(QLatin1String("ret")) ||
            lower == QLatin1String("endrepeat") ||
            lower == QLatin1String("endtell") ||
            lower == QLatin1String("starttell") ||
            lower == QLatin1String("invalid")) {
            return kDarkControl;
        }
        return std::nullopt;
    }
    if (lower.startsWith(QLatin1String("push"))) return kLightPush;
    if (lower.startsWith(QLatin1String("get"))) return kLightGet;
    if (lower.startsWith(QLatin1String("set"))) return kLightSet;
    if (lower.startsWith(QLatin1String("put"))) return kLightPut;
    if (lower.startsWith(QLatin1String("jmp")) ||
        lower.startsWith(QLatin1String("call")) ||
        lower.endsWith(QLatin1String("call")) ||
        lower.startsWith(QLatin1String("ret")) ||
        lower == QLatin1String("endrepeat") ||
        lower == QLatin1String("endtell") ||
        lower == QLatin1String("starttell") ||
        lower == QLatin1String("invalid")) {
        return kLightControl;
    }
    return std::nullopt;
}

QColor BytecodeHighlighter::offsetColor() {
    return darkBase() ? kDarkOffset : kLightOffset;
}

QColor BytecodeHighlighter::commentColor() {
    return darkBase() ? kDarkComment : kLightComment;
}

void BytecodeHighlighter::highlightBlock(const QString& text) {
    const auto match = lineRegex_.match(text);
    if (!match.hasMatch()) {
        return;
    }

    applyGutterMarker(text);

    if (!match.captured(3).isEmpty()) {
        QTextCharFormat offset;
        offset.setForeground(offsetColor());
        setFormat(match.capturedStart(3), match.capturedLength(3), offset);
    }

    if (auto color = categoryColor(match.captured(4))) {
        QTextCharFormat opcode;
        opcode.setForeground(*color);
        setFormat(match.capturedStart(4), match.capturedLength(4), opcode);
    }

    if (!match.captured(6).isEmpty()) {
        QTextCharFormat comment;
        comment.setForeground(commentColor());
        comment.setFontItalic(true);
        setFormat(match.capturedStart(6), match.capturedLength(6), comment);

        if (detail::kEnableDeclarationHighlighting) {
            const int base = match.capturedStart(6);
            auto nameMatch = kAnnotationIdentifier.globalMatch(match.captured(6));
            while (nameMatch.hasNext()) {
                const auto name = nameMatch.next();
                if (!declarationNames_.contains(name.captured().toLower())) {
                    continue;
                }
                QTextCharFormat underline;
                underline.setUnderlineStyle(QTextCharFormat::SingleUnderline);
                setFormat(base + name.capturedStart(), name.capturedLength(), underline);
            }
        }
    }
}

} // namespace libreshockwave::debugger
