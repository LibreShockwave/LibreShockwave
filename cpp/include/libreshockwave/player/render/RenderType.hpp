#pragma once

#include <string_view>

namespace libreshockwave::player::render {

enum class RenderType {
    Software
};

[[nodiscard]] std::string_view name(RenderType type);

} // namespace libreshockwave::player::render
