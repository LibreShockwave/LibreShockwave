#pragma once

#include <string_view>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::vm {
class LingoVM;
}

namespace libreshockwave::lingo::vm::parse {

class LingoExpressionParser {
public:
    [[nodiscard]] static Datum parse(std::string_view expression, LingoVM* vm = nullptr);
};

} // namespace libreshockwave::lingo::vm::parse
