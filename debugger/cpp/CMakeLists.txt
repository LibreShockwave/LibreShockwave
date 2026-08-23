cmake_minimum_required(VERSION 3.20)

add_executable(libreshockwave_debugger
    main.cpp
    DebuggerWindow.cpp
    DebuggerWindow.hpp
    StartupSession.cpp
    StartupSession.hpp
    ExternalParamsDialog.cpp
    ExternalParamsDialog.hpp
    DebuggerContext.cpp
    DebuggerContext.hpp
    DebugStateBridge.cpp
    DebugStateBridge.hpp
    ui/MovieTreePanel.cpp
    ui/MovieTreePanel.hpp
    ui/CodeViewPanel.cpp
    ui/CodeViewPanel.hpp
    ui/BreakpointGutter.cpp
    ui/BreakpointGutter.hpp
    ui/VariablesPanel.cpp
    ui/VariablesPanel.hpp
    ui/CallStackPanel.cpp
    ui/CallStackPanel.hpp
    ui/WatchPanel.cpp
    ui/WatchPanel.hpp
    ui/DebugWindowPanel.cpp
    ui/DebugWindowPanel.hpp
    ui/StageWidget.cpp
    ui/StageWidget.hpp
    format/DatumFormat.cpp
    format/DatumFormat.hpp
    model/DebuggerModel.cpp
    model/DebuggerModel.hpp
    model/SymbolIndex.cpp
    model/SymbolIndex.hpp
    ui/highlight/GutterHighlighter.cpp
    ui/highlight/GutterHighlighter.hpp
    ui/highlight/DeclarationHighlighting.hpp
    ui/highlight/LingoHighlighter.cpp
    ui/highlight/LingoHighlighter.hpp
    ui/highlight/BytecodeHighlighter.cpp
    ui/highlight/BytecodeHighlighter.hpp
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
        StartupSession.cpp
        StartupSession.hpp
        model/SymbolIndex.cpp
        model/SymbolIndex.hpp
        model/DebuggerModel.hpp
        ui/BreakpointGutter.cpp
        ui/BreakpointGutter.hpp
        ui/CodeViewPanel.cpp
        ui/CodeViewPanel.hpp
        ui/StageWidget.cpp
        ui/StageWidget.hpp
        ui/highlight/DeclarationHighlighting.hpp
        ui/highlight/GutterHighlighter.cpp
        ui/highlight/GutterHighlighter.hpp
        ui/highlight/LingoHighlighter.cpp
        ui/highlight/LingoHighlighter.hpp
        ui/highlight/BytecodeHighlighter.cpp
        ui/highlight/BytecodeHighlighter.hpp
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
