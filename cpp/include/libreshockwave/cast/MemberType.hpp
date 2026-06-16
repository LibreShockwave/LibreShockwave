#pragma once

#include <string_view>

namespace libreshockwave::cast {

enum class MemberType {
    Null = 0,
    Bitmap = 1,
    FilmLoop = 2,
    Text = 3,
    Palette = 4,
    Picture = 5,
    Sound = 6,
    Button = 7,
    Shape = 8,
    Movie = 9,
    DigitalVideo = 10,
    Script = 11,
    RichText = 12,
    Transition = 14,
    Xtra = 15,
    Font = 16,
    Shockwave3D = 17,
    Unknown = 255
};

[[nodiscard]] int code(MemberType type);
[[nodiscard]] std::string_view name(MemberType type);
[[nodiscard]] MemberType memberTypeFromCode(int code);

} // namespace libreshockwave::cast
