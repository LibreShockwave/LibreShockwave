#pragma once

namespace libreshockwave::player::input {

class DirectorKeyCodes final {
public:
    DirectorKeyCodes() = delete;

    [[nodiscard]] static int fromJavaKeyCode(int java_vk);
    [[nodiscard]] static int fromBrowserKeyCode(int browser_key_code);
};

} // namespace libreshockwave::player::input
