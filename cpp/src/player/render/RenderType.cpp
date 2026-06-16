#include "libreshockwave/player/render/RenderType.hpp"

#include <stdexcept>

namespace libreshockwave::player::render {

std::string_view name(RenderType type) {
    switch (type) {
        case RenderType::Software: return "SOFTWARE";
    }
    throw std::logic_error("Unknown RenderType enum value");
}

} // namespace libreshockwave::player::render
