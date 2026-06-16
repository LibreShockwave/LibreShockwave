#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo {

class LingoValueParser {
public:
    using IdentifierResolver = std::function<Datum(std::string_view identifier)>;

    [[nodiscard]] static Datum parseLiteral(std::string_view expression);
    [[nodiscard]] static std::optional<Datum> parseComplete(std::string_view expression,
                                                            IdentifierResolver identifierResolver = {});
    [[nodiscard]] static Datum parseWithPartial(std::string_view expression,
                                                IdentifierResolver identifierResolver = {});
};

} // namespace libreshockwave::lingo
