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
)

# Temporary diagnostic probe (remove before commit)
add_executable(player_probe player_probe.cpp)
target_link_libraries(player_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(player_probe PRIVATE cxx_std_20)

find_package(Qt6 QUIET COMPONENTS Network)
if(Qt6Network_FOUND)
    set(LIBRESHOCKWAVE_QT_NETWORK_TARGET Qt6::Network)
else()
    find_package(Qt5 QUIET COMPONENTS Network)
    if(Qt5Network_FOUND)
        set(LIBRESHOCKWAVE_QT_NETWORK_TARGET Qt5::Network)
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
endif()
