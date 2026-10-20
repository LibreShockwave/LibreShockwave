#pragma once

#include <string>

namespace libreshockwave::cast {

struct StyledSpan {
    int start_offset;
    int end_offset;
    std::string font_name;
    int font_size;
    bool bold;
    bool italic;
    bool underline;
    int color_r;
    int color_g;
    int color_b;

    friend bool operator==(const StyledSpan&, const StyledSpan&) = default;
};

} // namespace libreshockwave::cast
