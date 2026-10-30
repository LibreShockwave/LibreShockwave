cmake_minimum_required(VERSION 3.20)

add_executable(libreshockwave_debugger
    main.cpp
    debugger_window.cpp
    debugger_window.hpp
    startup_session.cpp
    startup_session.hpp
    external_params_dialog.cpp
    external_params_dialog.hpp
    debugger_context.cpp
    debugger_context.hpp
    debug_state_bridge.cpp
    debug_state_bridge.hpp
    ui/movie_tree_panel.cpp
    ui/movie_tree_panel.hpp
    ui/code_view_panel.cpp
    ui/code_view_panel.hpp
    ui/breakpoint_gutter.cpp
    ui/breakpoint_gutter.hpp
    ui/variables_panel.cpp
    ui/variables_panel.hpp
    ui/call_stack_panel.cpp
    ui/call_stack_panel.hpp
    ui/watch_panel.cpp
    ui/watch_panel.hpp
    ui/debug_window_panel.cpp
    ui/debug_window_panel.hpp
    ui/stage_widget.cpp
    ui/stage_widget.hpp
    format/datum_format.cpp
    format/datum_format.hpp
    model/debugger_model.cpp
    model/debugger_model.hpp
    model/symbol_index.cpp
    model/symbol_index.hpp
    ui/highlight/gutter_highlighter.cpp
    ui/highlight/gutter_highlighter.hpp
    ui/highlight/declaration_highlighting.hpp
    ui/highlight/lingo_highlighter.cpp
    ui/highlight/lingo_highlighter.hpp
    ui/highlight/bytecode_highlighter.cpp
    ui/highlight/bytecode_highlighter.hpp
    audio/native_audio_backend.cpp
    audio/native_audio_backend.hpp
)

find_package(Qt6 QUIET COMPONENTS Network)
if(Qt6Network_FOUND)
    set(LIBRESHOCKWAVE_QT_NETWORK_TARGET Qt6::Network)
else()
    find_package(Qt5 QUIET COMPONENTS Network)
    if(Qt5Network_FOUND)
        set(LIBRESHOCKWAVE_QT_NETWORK_TARGET Qt5::Network)
    endif()
endif()

# QTest lives in the Qt Test component; the merged debugger tests use it for
# synthetic mouse clicks.
find_package(Qt6 QUIET COMPONENTS Test)
if(Qt6Test_FOUND)
    set(LIBRESHOCKWAVE_QT_TEST_TARGET Qt6::Test)
else()
    find_package(Qt5 QUIET COMPONENTS Test)
    if(Qt5Test_FOUND)
        set(LIBRESHOCKWAVE_QT_TEST_TARGET Qt5::Test)
    endif()
endif()

# Native sound output is optional. The backend uses libmpg123 for MP3 decoding
# and the ALSA runtime library through dlopen for playback.
find_path(LIBRESHOCKWAVE_MPG123_INCLUDE_DIR mpg123.h)
find_library(LIBRESHOCKWAVE_MPG123_LIBRARY NAMES mpg123)
if(UNIX AND NOT APPLE AND LIBRESHOCKWAVE_MPG123_INCLUDE_DIR AND LIBRESHOCKWAVE_MPG123_LIBRARY)
    target_compile_definitions(libreshockwave_debugger PRIVATE LIBRESHOCKWAVE_DEBUGGER_HAS_AUDIO=1)
    target_include_directories(libreshockwave_debugger PRIVATE ${LIBRESHOCKWAVE_MPG123_INCLUDE_DIR})
    target_link_libraries(libreshockwave_debugger PRIVATE ${LIBRESHOCKWAVE_MPG123_LIBRARY} ${CMAKE_DL_LIBS})
endif()

target_link_libraries(libreshockwave_debugger
    PRIVATE
        LibreShockwave::libreshockwave
        ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET}
)
if(DEFINED LIBRESHOCKWAVE_QT_NETWORK_TARGET)
    target_link_libraries(libreshockwave_debugger PRIVATE ${LIBRESHOCKWAVE_QT_NETWORK_TARGET})
endif()

target_include_directories(libreshockwave_debugger PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(libreshockwave_debugger PRIVATE cxx_std_20)
set_target_properties(libreshockwave_debugger PROPERTIES AUTOMOC ON)

if(BUILD_TESTING)
    # All debugger coverage lives in this one binary and test file. Extend
    # debugger_test.cpp instead of adding new CMake test targets.
    add_executable(libreshockwave_debugger_tests
        startup_session.cpp
        startup_session.hpp
        model/symbol_index.cpp
        model/symbol_index.hpp
        model/debugger_model.hpp
        ui/breakpoint_gutter.cpp
        ui/breakpoint_gutter.hpp
        ui/code_view_panel.cpp
        ui/code_view_panel.hpp
        ui/stage_widget.cpp
        ui/stage_widget.hpp
        ui/highlight/declaration_highlighting.hpp
        ui/highlight/gutter_highlighter.cpp
        ui/highlight/gutter_highlighter.hpp
        ui/highlight/lingo_highlighter.cpp
        ui/highlight/lingo_highlighter.hpp
        ui/highlight/bytecode_highlighter.cpp
        ui/highlight/bytecode_highlighter.hpp
        debugger_test.cpp
    )
    target_link_libraries(libreshockwave_debugger_tests
        PRIVATE
            LibreShockwave::libreshockwave
            ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET}
            ${LIBRESHOCKWAVE_QT_TEST_TARGET}
        )
    target_include_directories(libreshockwave_debugger_tests
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(libreshockwave_debugger_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_tests PRIVATE -UNDEBUG)
    set_target_properties(libreshockwave_debugger_tests PROPERTIES AUTOMOC ON)
    add_test(NAME libreshockwave_debugger_tests
             COMMAND libreshockwave_debugger_tests)
endif()
