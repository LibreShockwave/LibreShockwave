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

# QTest lives in the Qt Test component; only the code-view test uses it.
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
    add_executable(libreshockwave_debugger_startup_tests
        StartupSession.cpp
        StartupSession.hpp
        startup_session_test.cpp
    )
    target_link_libraries(libreshockwave_debugger_startup_tests
        PRIVATE ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET})
    target_compile_features(libreshockwave_debugger_startup_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_startup_tests PRIVATE -UNDEBUG)
    add_test(NAME libreshockwave_debugger_startup_tests
             COMMAND libreshockwave_debugger_startup_tests)

    # SymbolIndex is pure C++; Qt Core is linked only because the model
    # header declares Qt metatypes.
    add_executable(libreshockwave_debugger_symbol_index_tests
        model/SymbolIndex.cpp
        model/SymbolIndex.hpp
        model/DebuggerModel.hpp
        symbol_index_test.cpp
    )
    target_link_libraries(libreshockwave_debugger_symbol_index_tests
        PRIVATE ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET})
    target_include_directories(libreshockwave_debugger_symbol_index_tests
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(libreshockwave_debugger_symbol_index_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_symbol_index_tests PRIVATE -UNDEBUG)
    add_test(NAME libreshockwave_debugger_symbol_index_tests
             COMMAND libreshockwave_debugger_symbol_index_tests)

    # Highlighter tests drive QSyntaxHighlighter through a QTextDocument.
    add_executable(libreshockwave_debugger_highlight_tests
        ui/highlight/GutterHighlighter.cpp
        ui/highlight/GutterHighlighter.hpp
        ui/highlight/DeclarationHighlighting.hpp
        ui/highlight/LingoHighlighter.cpp
        ui/highlight/LingoHighlighter.hpp
        ui/highlight/BytecodeHighlighter.cpp
        ui/highlight/BytecodeHighlighter.hpp
        model/DebuggerModel.hpp
        highlight_test.cpp
    )
    target_link_libraries(libreshockwave_debugger_highlight_tests
        PRIVATE ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET})
    target_include_directories(libreshockwave_debugger_highlight_tests
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(libreshockwave_debugger_highlight_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_highlight_tests PRIVATE -UNDEBUG)
    set_target_properties(libreshockwave_debugger_highlight_tests PROPERTIES AUTOMOC ON)
    add_test(NAME libreshockwave_debugger_highlight_tests
             COMMAND libreshockwave_debugger_highlight_tests)

    # Code view panel: gutter breakpoint state (current-line marker, the -1
    # structural sentinel, and the red-dot Breakpoint state across both tabs),
    # the click-to-toggle path, and the right-click "Go to declaration" menu.  The panel is a QWidget, so the
    # test runs under QApplication offscreen; it uses only Qt + the debugger
    # model, so no libreshockwave link is needed.
    add_executable(libreshockwave_debugger_code_view_tests
        code_view_test.cpp
        ui/CodeViewPanel.cpp
        ui/CodeViewPanel.hpp
        ui/BreakpointGutter.cpp
        ui/BreakpointGutter.hpp
        ui/highlight/GutterHighlighter.cpp
        ui/highlight/GutterHighlighter.hpp
        ui/highlight/DeclarationHighlighting.hpp
        ui/highlight/LingoHighlighter.cpp
        ui/highlight/LingoHighlighter.hpp
        ui/highlight/BytecodeHighlighter.cpp
        ui/highlight/BytecodeHighlighter.hpp
        model/SymbolIndex.cpp
        model/SymbolIndex.hpp
        model/DebuggerModel.hpp
    )
    target_link_libraries(libreshockwave_debugger_code_view_tests
        PRIVATE
            ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET}
            ${LIBRESHOCKWAVE_QT_TEST_TARGET}
        )
    target_include_directories(libreshockwave_debugger_code_view_tests
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(libreshockwave_debugger_code_view_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_code_view_tests PRIVATE -UNDEBUG)
    set_target_properties(libreshockwave_debugger_code_view_tests PROPERTIES AUTOMOC ON)
    add_test(NAME libreshockwave_debugger_code_view_tests
             COMMAND libreshockwave_debugger_code_view_tests)

    # StageWidget key forwarding: Qt key events must be translated to Director
    # key codes before reaching the input callback.  Needs the core library
    # for DirectorKeyCodes; runs under QApplication offscreen.
    add_executable(libreshockwave_debugger_stage_widget_tests
        stage_widget_test.cpp
        ui/StageWidget.cpp
        ui/StageWidget.hpp
    )
    target_link_libraries(libreshockwave_debugger_stage_widget_tests
        PRIVATE
            LibreShockwave::libreshockwave
            ${LIBRESHOCKWAVE_QT_WIDGETS_TARGET}
            ${LIBRESHOCKWAVE_QT_TEST_TARGET}
        )
    target_include_directories(libreshockwave_debugger_stage_widget_tests
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(libreshockwave_debugger_stage_widget_tests PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_debugger_stage_widget_tests PRIVATE -UNDEBUG)
    set_target_properties(libreshockwave_debugger_stage_widget_tests PROPERTIES AUTOMOC ON)
    add_test(NAME libreshockwave_debugger_stage_widget_tests
             COMMAND libreshockwave_debugger_stage_widget_tests)
endif()
