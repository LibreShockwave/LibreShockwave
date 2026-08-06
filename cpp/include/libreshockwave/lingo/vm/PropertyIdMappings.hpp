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
    // Cast-member property names (legacy GET/SET property types 0x09/0x0a/
    // 0x0b/0x0c). Mirrors the decompiler's memberPropertyName table.
    [[nodiscard]] static std::string_view getCastMemberPropName(int id);
};

} // namespace libreshockwave::lingo::vm
