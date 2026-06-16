if(NOT DEFINED MAC_FONT_DIR OR NOT DEFINED WINDOWS_FONT_DIR OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "MAC_FONT_DIR, WINDOWS_FONT_DIR, and OUTPUT_FILE are required")
endif()

foreach(font_dir IN ITEMS "${MAC_FONT_DIR}" "${WINDOWS_FONT_DIR}")
    if(NOT IS_DIRECTORY "${font_dir}")
        message(FATAL_ERROR "Missing bundled platform font directory: ${font_dir}")
    endif()
endforeach()

function(hex_to_initializer hex_value output_var)
    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " bytes "${hex_value}")
    string(REGEX REPLACE "(.{96})" "    \\1\n" bytes "${bytes}")
    set("${output_var}" "    ${bytes}" PARENT_SCOPE)
endfunction()

function(emit_font_dataset category font_dir output_entries_var)
    file(GLOB font_files "${font_dir}/*.ttf")
    list(SORT font_files)

    set(entries "")
    set(index 0)
    foreach(font_file IN LISTS font_files)
        if(NOT EXISTS "${font_file}")
            message(FATAL_ERROR "Missing bundled platform font resource: ${font_file}")
        endif()

        get_filename_component(font_stem "${font_file}" NAME_WE)
        string(TOLOWER "${font_stem}" font_key)
        file(READ "${font_file}" font_hex HEX)
        file(SIZE "${font_file}" font_size)
        hex_to_initializer("${font_hex}" font_bytes)

        set(array_symbol "k${category}Font${index}")
        set(accessor_symbol "${category}Font${index}Data")
        file(APPEND "${OUTPUT_FILE}" "constexpr std::array<std::uint8_t, ${font_size}> ${array_symbol} = {\n${font_bytes}\n};\n\n")
        file(APPEND "${OUTPUT_FILE}" "const std::vector<std::uint8_t>& ${accessor_symbol}() {\n")
        file(APPEND "${OUTPUT_FILE}" "    static const std::vector<std::uint8_t> data(${array_symbol}.begin(), ${array_symbol}.end());\n")
        file(APPEND "${OUTPUT_FILE}" "    return data;\n")
        file(APPEND "${OUTPUT_FILE}" "}\n\n")

        string(APPEND entries "    if (dataKey == \"${font_key}\") {\n")
        string(APPEND entries "        return &${accessor_symbol}();\n")
        string(APPEND entries "    }\n")
        math(EXPR index "${index} + 1")
    endforeach()

    set("${output_entries_var}" "${entries}" PARENT_SCOPE)
endfunction()

get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_FILE}" [[
#include "libreshockwave/fonts/PlatformFonts.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace libreshockwave::fonts::platform {
namespace {

]])

emit_font_dataset("Mac" "${MAC_FONT_DIR}" MAC_ENTRIES)
emit_font_dataset("Windows" "${WINDOWS_FONT_DIR}" WINDOWS_ENTRIES)

file(APPEND "${OUTPUT_FILE}" [[
} // namespace

const std::vector<std::uint8_t>* macFontData(std::string_view dataKey) {
]])
file(APPEND "${OUTPUT_FILE}" "${MAC_ENTRIES}")
file(APPEND "${OUTPUT_FILE}" [[
    return nullptr;
}

const std::vector<std::uint8_t>* windowsFontData(std::string_view dataKey) {
]])
file(APPEND "${OUTPUT_FILE}" "${WINDOWS_ENTRIES}")
file(APPEND "${OUTPUT_FILE}" [[
    return nullptr;
}

} // namespace libreshockwave::fonts::platform
]])
