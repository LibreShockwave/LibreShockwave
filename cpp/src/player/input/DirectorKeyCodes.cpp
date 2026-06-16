#include "libreshockwave/player/input/DirectorKeyCodes.hpp"

#include <array>

namespace libreshockwave::player::input {
namespace {

constexpr int VK_ENTER = 10;
constexpr int VK_BACK_SPACE = 8;
constexpr int VK_TAB = 9;
constexpr int VK_ESCAPE = 27;
constexpr int VK_SPACE = 32;
constexpr int VK_DELETE = 127;
constexpr int VK_LEFT = 37;
constexpr int VK_UP = 38;
constexpr int VK_RIGHT = 39;
constexpr int VK_DOWN = 40;
constexpr int VK_HOME = 36;
constexpr int VK_END = 35;
constexpr int VK_PAGE_UP = 33;
constexpr int VK_PAGE_DOWN = 34;
constexpr int VK_F1 = 112;
constexpr int VK_F2 = 113;
constexpr int VK_F3 = 114;
constexpr int VK_F4 = 115;
constexpr int VK_F5 = 116;
constexpr int VK_F6 = 117;
constexpr int VK_F7 = 118;
constexpr int VK_F8 = 119;
constexpr int VK_F9 = 120;
constexpr int VK_F10 = 121;
constexpr int VK_F11 = 122;
constexpr int VK_F12 = 123;
constexpr int VK_A = 65;
constexpr int VK_0 = 48;

constexpr std::array<int, 26> kMacLetterCodes{{
    0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46,
    45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6
}};

constexpr std::array<int, 10> kMacDigitCodes{{29, 18, 19, 20, 21, 23, 22, 26, 28, 25}};

int macLetterCode(int letterIndex) {
    if (letterIndex >= 0 && letterIndex < static_cast<int>(kMacLetterCodes.size())) {
        return kMacLetterCodes[static_cast<std::size_t>(letterIndex)];
    }
    return 0;
}

int macDigitCode(int digitIndex) {
    if (digitIndex >= 0 && digitIndex < static_cast<int>(kMacDigitCodes.size())) {
        return kMacDigitCodes[static_cast<std::size_t>(digitIndex)];
    }
    return 0;
}

} // namespace

int DirectorKeyCodes::fromJavaKeyCode(int javaVK) {
    switch (javaVK) {
        case VK_ENTER: return 36;
        case VK_TAB: return 48;
        case VK_SPACE: return 49;
        case VK_BACK_SPACE: return 51;
        case VK_ESCAPE: return 53;
        case VK_DELETE: return 117;
        case VK_LEFT: return 123;
        case VK_RIGHT: return 124;
        case VK_DOWN: return 125;
        case VK_UP: return 126;
        case VK_HOME: return 115;
        case VK_END: return 119;
        case VK_PAGE_UP: return 116;
        case VK_PAGE_DOWN: return 121;
        case VK_F1: return 122;
        case VK_F2: return 120;
        case VK_F3: return 99;
        case VK_F4: return 118;
        case VK_F5: return 96;
        case VK_F6: return 97;
        case VK_F7: return 98;
        case VK_F8: return 100;
        case VK_F9: return 101;
        case VK_F10: return 109;
        case VK_F11: return 103;
        case VK_F12: return 111;
        default:
            if (javaVK >= VK_A && javaVK <= VK_A + 25) {
                return macLetterCode(javaVK - VK_A);
            }
            if (javaVK >= VK_0 && javaVK <= VK_0 + 9) {
                return macDigitCode(javaVK - VK_0);
            }
            return javaVK;
    }
}

int DirectorKeyCodes::fromBrowserKeyCode(int browserKeyCode) {
    switch (browserKeyCode) {
        case 8: return 51;
        case 9: return 48;
        case 13: return 36;
        case 27: return 53;
        case 32: return 49;
        case 33: return 116;
        case 34: return 121;
        case 35: return 119;
        case 36: return 115;
        case 37: return 123;
        case 38: return 126;
        case 39: return 124;
        case 40: return 125;
        case 46: return 117;
        case 112: return 122;
        case 113: return 120;
        case 114: return 99;
        case 115: return 118;
        case 116: return 96;
        case 117: return 97;
        case 118: return 98;
        case 119: return 100;
        case 120: return 101;
        case 121: return 109;
        case 122: return 103;
        case 123: return 111;
        default:
            if (browserKeyCode >= 65 && browserKeyCode <= 90) {
                return macLetterCode(browserKeyCode - 65);
            }
            if (browserKeyCode >= 48 && browserKeyCode <= 57) {
                return macDigitCode(browserKeyCode - 48);
            }
            return browserKeyCode;
    }
}

} // namespace libreshockwave::player::input
