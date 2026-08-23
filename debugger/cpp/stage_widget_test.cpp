// StageWidget key forwarding: Qt key events must reach the input callback as
// Director key codes (the same codes the WASM bridge produces), so editable
// fields and Lingo `keyDown` handlers see Backspace=51, Enter=36, arrows, etc.
#include <QApplication>
#include <QKeyEvent>
#include <QString>
#include <QTest>

#include "ui/StageWidget.hpp"

#include <cassert>
#include <string>
#include <vector>

using libreshockwave::debugger::StageWidget;

namespace {

constexpr int kKeyDown = 3;
constexpr int kKeyUp = 4;

struct CapturedKey {
    int type;
    int keyCode;
    std::string text;
};

class EventSpy {
public:
    void record(int type, int stageX, int stageY, int keyCode,
                const std::string& keyText, bool /*shift*/, bool /*ctrl*/,
                bool /*alt*/, bool /*rightButton*/) {
        (void)stageX;
        (void)stageY;
        captured_.push_back(CapturedKey{type, keyCode, keyText});
    }

    const std::vector<CapturedKey>& keys() const { return captured_; }
    void clear() { captured_.clear(); }

private:
    std::vector<CapturedKey> captured_;
};

void attachSpy(StageWidget& widget, EventSpy& spy) {
    widget.setInputCallback(
        [&spy](int type, int stageX, int stageY, int keyCode,
               const std::string& keyText, bool shift, bool ctrl, bool alt,
               bool rightButton) {
            spy.record(type, stageX, stageY, keyCode, keyText, shift, ctrl,
                       alt, rightButton);
        });
}

void sendKey(StageWidget& widget, int qtKey, const QString& text) {
    QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, qtKey, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QApplication::sendEvent(&widget, &release);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // Enter and Backspace must arrive as Director codes 36 and 51; before the
    // fix they were forwarded as raw browser codes 13 and 8 and were ignored
    // by editable-field input handling.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_Return, QStringLiteral("\r"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].type == kKeyDown && spy.keys()[0].keyCode == 36);
        assert(spy.keys()[1].type == kKeyUp && spy.keys()[1].keyCode == 36);

        spy.clear();
        sendKey(widget, Qt::Key_Backspace, QStringLiteral("\b"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].type == kKeyDown && spy.keys()[0].keyCode == 51);
        assert(spy.keys()[1].type == kKeyUp && spy.keys()[1].keyCode == 51);
    }

    // Keypad Enter shares the Return code.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier);
        QApplication::sendEvent(&widget, &press);
        assert(spy.keys().size() == 1);
        assert(spy.keys()[0].keyCode == 36);
    }

    // Navigation and editing keys use Director codes, not browser codes.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        const std::pair<int, int> arrowCases[] = {
            {Qt::Key_Left, 123}, {Qt::Key_Right, 124}, {Qt::Key_Up, 126},
            {Qt::Key_Down, 125}, {Qt::Key_Tab, 48},    {Qt::Key_Escape, 53},
            {Qt::Key_Space, 49}, {Qt::Key_Delete, 117}, {Qt::Key_F1, 122},
        };
        for (const auto& [qtKey, directorCode] : arrowCases) {
            spy.clear();
            sendKey(widget, qtKey, QString());
            assert(spy.keys().size() == 2);
            assert(spy.keys()[0].type == kKeyDown);
            assert(spy.keys()[0].keyCode == directorCode);
            assert(spy.keys()[1].keyCode == directorCode);
        }
    }

    // Printable keys keep their character text while the key code becomes the
    // Director Mac code (letter A -> 0, digit 5 -> 23).
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_A, QStringLiteral("a"));
        assert(spy.keys().size() == 2);
        assert(spy.keys()[0].keyCode == 0);
        assert(spy.keys()[0].text == "a");

        spy.clear();
        sendKey(widget, Qt::Key_5, QStringLiteral("5"));
        assert(spy.keys()[0].keyCode == 23);
        assert(spy.keys()[0].text == "5");
    }

    // Shifted punctuation (reported as its own Latin-1 key on Linux) must be
    // forwarded with its text; the code lands in the private printable range
    // instead of colliding with DOM navigation codes.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        const std::pair<int, QString> symbolCases[] = {
            {Qt::Key_Exclam, QStringLiteral("!")},
            {Qt::Key_At, QStringLiteral("@")},
            {Qt::Key_NumberSign, QStringLiteral("#")},
            {Qt::Key_Percent, QStringLiteral("%")},
            {Qt::Key_Ampersand, QStringLiteral("&")},
            {Qt::Key_Asterisk, QStringLiteral("*")},
            {Qt::Key_ParenLeft, QStringLiteral("(")},
            {Qt::Key_Colon, QStringLiteral(":")},
            {Qt::Key_QuoteDbl, QStringLiteral("\"")},
            {Qt::Key_Less, QStringLiteral("<")},
            {Qt::Key_Greater, QStringLiteral(">")},
            {Qt::Key_Question, QStringLiteral("?")},
        };
        for (const auto& [qtKey, text] : symbolCases) {
            spy.clear();
            sendKey(widget, qtKey, text);
            assert(spy.keys().size() == 2);
            const int expectedCode = 0x2000 + qtKey;
            assert(spy.keys()[0].type == kKeyDown);
            assert(spy.keys()[0].keyCode == expectedCode);
            assert(spy.keys()[0].text == text.toStdString());
            assert(spy.keys()[1].keyCode == expectedCode);
        }
    }

    // Keys with no mapping produce no callback events at all.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        sendKey(widget, Qt::Key_Super_L, QString());
        assert(spy.keys().empty());
    }

    // Auto-repeat presses are suppressed like ordinary repeat handling.
    {
        EventSpy spy;
        StageWidget widget;
        attachSpy(widget, spy);
        QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                              QStringLiteral("b"), true);
        QApplication::sendEvent(&widget, &repeatPress);
        assert(spy.keys().empty());
    }

    return 0;
}
