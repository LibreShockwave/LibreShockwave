if(NOT DEFINED INPUT_REGULAR OR NOT DEFINED INPUT_BOLD OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_REGULAR, INPUT_BOLD, and OUTPUT_FILE are required")
endif()

foreach(font_file IN ITEMS "${INPUT_REGULAR}" "${INPUT_BOLD}")
    if(NOT EXISTS "${font_file}")
        message(FATAL_ERROR "Missing bundled font resource: ${font_file}")
    endif()
endforeach()

file(READ "${INPUT_REGULAR}" REGULAR_HEX HEX)
file(READ "${INPUT_BOLD}" BOLD_HEX HEX)
file(SIZE "${INPUT_REGULAR}" REGULAR_SIZE)
file(SIZE "${INPUT_BOLD}" BOLD_SIZE)

function(hex_to_initializer hex_value output_var)
    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " bytes "${hex_value}")
    set("${output_var}" "${bytes}" PARENT_SCOPE)
endfunction()

hex_to_initializer("${REGULAR_HEX}" REGULAR_BYTES)
hex_to_initializer("${BOLD_HEX}" BOLD_BYTES)

get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_FILE}" [[
#include "libreshockwave/fonts/volter/Volter.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace libreshockwave::fonts::volter {
namespace {

constexpr std::array<std::uint8_t, ]] "${REGULAR_SIZE}" [[> kVolterRegular = {
]] "${REGULAR_BYTES}" [[
};

constexpr std::array<std::uint8_t, ]] "${BOLD_SIZE}" [[> kVolterBold = {
]] "${BOLD_BYTES}" [[
};

} // namespace

const std::vector<std::uint8_t>& regularData() {
    static const std::vector<std::uint8_t> data(kVolterRegular.begin(), kVolterRegular.end());
    return data;
}

const std::vector<std::uint8_t>& boldData() {
    static const std::vector<std::uint8_t> data(kVolterBold.begin(), kVolterBold.end());
    return data;
}

} // namespace libreshockwave::fonts::volter
]])
