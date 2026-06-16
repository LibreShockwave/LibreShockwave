#pragma once

namespace libreshockwave::player::render {

class RenderConfig final {
public:
    RenderConfig() = delete;

    [[nodiscard]] static bool isAntialias();
    static void setAntialias(bool enabled);

private:
    static bool antialias_;
};

} // namespace libreshockwave::player::render
