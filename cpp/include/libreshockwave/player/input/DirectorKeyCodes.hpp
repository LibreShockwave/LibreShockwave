#pragma once

namespace libreshockwave::player::input {

class DirectorKeyCodes final {
public:
    DirectorKeyCodes() = delete;

    [[nodiscard]] static int fromJavaKeyCode(int javaVK);
    [[nodiscard]] static int fromBrowserKeyCode(int browserKeyCode);
};

} // namespace libreshockwave::player::input
