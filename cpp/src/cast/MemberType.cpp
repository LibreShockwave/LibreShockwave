#include "libreshockwave/cast/MemberType.hpp"

namespace libreshockwave::cast {

int code(MemberType type) {
    return static_cast<int>(type);
}

std::string_view name(MemberType type) {
    switch (type) {
        case MemberType::Null: return "null";
        case MemberType::Bitmap: return "bitmap";
        case MemberType::FilmLoop: return "filmLoop";
        case MemberType::Text: return "text";
        case MemberType::Palette: return "palette";
        case MemberType::Picture: return "picture";
        case MemberType::Sound: return "sound";
        case MemberType::Button: return "button";
        case MemberType::Shape: return "shape";
        case MemberType::Movie: return "movie";
        case MemberType::DigitalVideo: return "digitalVideo";
        case MemberType::Script: return "script";
        case MemberType::RichText: return "richText";
        case MemberType::Transition: return "transition";
        case MemberType::Xtra: return "xtra";
        case MemberType::Font: return "font";
        case MemberType::Shockwave3D: return "shockwave3d";
        case MemberType::Unknown: return "unknown";
    }
    return "unknown";
}

MemberType memberTypeFromCode(int value) {
    switch (value) {
        case 0: return MemberType::Null;
        case 1: return MemberType::Bitmap;
        case 2: return MemberType::FilmLoop;
        case 3: return MemberType::Text;
        case 4: return MemberType::Palette;
        case 5: return MemberType::Picture;
        case 6: return MemberType::Sound;
        case 7: return MemberType::Button;
        case 8: return MemberType::Shape;
        case 9: return MemberType::Movie;
        case 10: return MemberType::DigitalVideo;
        case 11: return MemberType::Script;
        case 12: return MemberType::RichText;
        case 14: return MemberType::Transition;
        case 15: return MemberType::Xtra;
        case 16: return MemberType::Font;
        case 17: return MemberType::Shockwave3D;
        default: return MemberType::Unknown;
    }
}

} // namespace libreshockwave::cast
