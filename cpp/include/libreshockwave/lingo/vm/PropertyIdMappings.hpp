#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace libreshockwave::lingo::vm {

class PropertyIdMappings {
public:
    [[nodiscard]] static std::optional<std::string_view> getMoviePropName(int id);
    [[nodiscard]] static std::optional<std::string_view> getSpritePropName(int id);
    [[nodiscard]] static std::optional<std::string_view> getAnimPropName(int id);
    [[nodiscard]] static std::optional<std::string_view> getAnim2PropName(int id);
    [[nodiscard]] static std::string getSoundPropName(int id);
};

} // namespace libreshockwave::lingo::vm
