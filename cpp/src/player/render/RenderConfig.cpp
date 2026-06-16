#include "libreshockwave/player/render/RenderConfig.hpp"

namespace libreshockwave::player::render {

bool RenderConfig::antialias_ = false;

bool RenderConfig::isAntialias() {
    return antialias_;
}

void RenderConfig::setAntialias(bool enabled) {
    antialias_ = enabled;
}

} // namespace libreshockwave::player::render
