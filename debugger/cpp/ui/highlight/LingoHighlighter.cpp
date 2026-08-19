#include "LingoHighlighter.hpp"

#include <QTextCharFormat>

namespace libreshockwave::debugger {

namespace {

// Lingo control-flow and statement keywords (Lingo is case-insensitive, so
// the sets and all lookups use lower-cased words).
const QSet<QString> kKeywords = {
    "on", "end", "if", "then", "else", "put", "set", "into", "before",
    "after", "behind", "in", "front", "of", "repeat", "while", "forever",
    "with", "to", "down", "by", "case", "cases", "or", "and", "not",
    "otherwise", "exit", "next", "tell", "when", "return", "local",
    "global", "property", "the",
};

// Lingo builtin words: constants, object types, and common environment
// properties/functions that ship with the runtime.
const QSet<QString> kBuiltins = {
    "me", "it", "this", "true", "false", "void", "space", "pi", "mouseLoc",
    "key", "char", "stage", "sprite", "button", "field", "member", "window",
    "movie", "score", "frame", "boundary", "color", "palette", "white",
    "black", "red", "green", "blue", "time", "secs", "frames", "mouseDown",
    "mouseX", "mouseY", "mouseBtn",
};

// Display-asset colors (VS Code light/dark theme inspired).
const QColor kLightKeyword(0x00, 0x00, 0xff);
const QColor kLightBuiltin(0x0f, 0x76, 0x6e);
const QColor kLightString(0xa3, 0x15, 0x15);
const QColor kLightNumber(0x09, 0x86, 0x58);
const QColor kLightSymbol(0x6d, 0x28, 0xd9);
const QColor kLightMethod(0x79, 0x5e, 0x26);
const QColor kDarkKeyword(0x56, 0x9c, 0xd6);
const QColor kDarkBuiltin(0x4e, 0xc9, 0xb0);
const QColor kDarkString(0xce, 0x91, 0x78);
const QColor kDarkNumber(0xb5, 0xce, 0xa8);
const QColor kDarkSymbol(0xc5, 0x86, 0xc0);
const QColor kDarkMethod(0xdc, 0xdc, 0xaa);

} // namespace

LingoHighlighter::LingoHighlighter(QTextDocument* document)
    : GutterHighlighter(document),
      // Groups: 1 = string (incl. quotes), 2 = #symbol, 3 = number,
      // 4 = identifier.
      tokenRegex_(QRegularExpression(
          "(\"(?:[^\"\\\\]|\\\\.)*\")|(#[A-Za-z_]\\w*)"
          "|(\\b\\d+(?:\\.\\d+)?\\b)|([A-Za-z_]\\w*)")) {
}

void LingoHighlighter::setMethodNames(const QSet<QString>& names) {
    if (names == methodNames_) {
        return;
    }
    methodNames_ = names;
    rehighlight();
}

void LingoHighlighter::setDeclarationNames(const QSet<QString>& names) {
    if (names == declarationNames_) {
        return;
    }
    declarationNames_ = names;
    rehighlight();
}

void LingoHighlighter::setVariableNames(const QSet<QString>& names) {
    if (names == variableNames_) {
        return;
    }
    variableNames_ = names;
    rehighlight();
}

QColor LingoHighlighter::keywordColor() {
    return darkBase() ? kDarkKeyword : kLightKeyword;
}

QColor LingoHighlighter::builtinColor() {
    return darkBase() ? kDarkBuiltin : kLightBuiltin;
}

QColor LingoHighlighter::stringColor() {
    return darkBase() ? kDarkString : kLightString;
}

QColor LingoHighlighter::numberColor() {
    return darkBase() ? kDarkNumber : kLightNumber;
}

QColor LingoHighlighter::symbolColor() {
    return darkBase() ? kDarkSymbol : kLightSymbol;
}

QColor LingoHighlighter::methodColor() {
    return darkBase() ? kDarkMethod : kLightMethod;
}

std::optional<QColor> LingoHighlighter::identifierColor(const QString& lowerName,
                                                        const QSet<QString>& methods) {
    if (kKeywords.contains(lowerName)) return keywordColor();
    if (kBuiltins.contains(lowerName)) return builtinColor();
    if (methods.contains(lowerName)) return methodColor();
    return std::nullopt;
}

void LingoHighlighter::highlightBlock(const QString& text) {
    applyGutterMarker(text);

    auto match = tokenRegex_.globalMatch(text);
    while (match.hasNext()) {
        const auto capture = match.next();
        QTextCharFormat format;
        bool hasFormat = false;
        if (!capture.captured(1).isEmpty()) {
            format.setForeground(stringColor());
            hasFormat = true;
        } else if (!capture.captured(2).isEmpty()) {
            format.setForeground(symbolColor());
            hasFormat = true;
        } else if (!capture.captured(3).isEmpty()) {
            format.setForeground(numberColor());
            hasFormat = true;
        } else {
            const QString lower = capture.captured(4).toLower();
            if (auto color = identifierColor(lower, methodNames_)) {
                format.setForeground(*color);
                hasFormat = true;
            }
            // Underline the words the right-click "Go to declaration"
            // resolves: current-handler variables (dotted) first, then the
            // movie-wide declarations (single).
            if (variableNames_.contains(lower)) {
                format.setUnderlineStyle(QTextCharFormat::DotLine);
            } else if (declarationNames_.contains(lower)) {
                format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            }
            if (format.underlineStyle() != QTextCharFormat::NoUnderline) {
                hasFormat = true;
            }
        }
        if (hasFormat) {
            setFormat(capture.capturedStart(), capture.capturedLength(), format);
        }
    }
}

} // namespace libreshockwave::debugger
