set(LIBRESHOCKWAVE_VOLTER_DATA_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/src/fonts/volter/volter_data.cpp")
set(LIBRESHOCKWAVE_VOLTER_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/volter")
add_custom_command(
    OUTPUT "${LIBRESHOCKWAVE_VOLTER_DATA_CPP}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_REGULAR=${LIBRESHOCKWAVE_VOLTER_RESOURCE_DIR}/volter.ttf"
        "-DINPUT_BOLD=${LIBRESHOCKWAVE_VOLTER_RESOURCE_DIR}/volter_bold.ttf"
        "-DOUTPUT_FILE=${LIBRESHOCKWAVE_VOLTER_DATA_CPP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GenerateVolterData.cmake"
    DEPENDS
        "${LIBRESHOCKWAVE_VOLTER_RESOURCE_DIR}/volter.ttf"
        "${LIBRESHOCKWAVE_VOLTER_RESOURCE_DIR}/volter_bold.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GenerateVolterData.cmake"
    VERBATIM
)
set_source_files_properties("${LIBRESHOCKWAVE_VOLTER_DATA_CPP}" PROPERTIES GENERATED TRUE)

set(LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/src/fonts/platform_font_data.cpp")
set(LIBRESHOCKWAVE_PLATFORM_FONT_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
file(GLOB LIBRESHOCKWAVE_PLATFORM_FONT_FILES CONFIGURE_DEPENDS
    "${LIBRESHOCKWAVE_PLATFORM_FONT_RESOURCE_DIR}/mac/*.ttf"
    "${LIBRESHOCKWAVE_PLATFORM_FONT_RESOURCE_DIR}/windows/*.ttf"
)
add_custom_command(
    OUTPUT "${LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP}"
    COMMAND "${CMAKE_COMMAND}"
        "-DMAC_FONT_DIR=${LIBRESHOCKWAVE_PLATFORM_FONT_RESOURCE_DIR}/mac"
        "-DWINDOWS_FONT_DIR=${LIBRESHOCKWAVE_PLATFORM_FONT_RESOURCE_DIR}/windows"
        "-DOUTPUT_FILE=${LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GeneratePlatformFontData.cmake"
    DEPENDS
        ${LIBRESHOCKWAVE_PLATFORM_FONT_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GeneratePlatformFontData.cmake"
    VERBATIM
)
set_source_files_properties("${LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP}" PROPERTIES GENERATED TRUE)

add_library(libreshockwave
    src/w3d_file.cpp
    src/audio/sound_converter.cpp
    src/bitmap/bitmap.cpp
    src/bitmap/bitmap_colorizer.cpp
    src/bitmap/bitmap_decoder.cpp
    src/bitmap/color_ref.cpp
    src/bitmap/drawing.cpp
    src/bitmap/gif_decoder.cpp
    src/bitmap/palette.cpp
    src/bitmap/png_decoder.cpp
    src/cast/bitmap_info.cpp
    src/cast/cast_member.cpp
    src/cast/film_loop_info.cpp
    src/cast/member_type.cpp
    src/cast/script_type.cpp
    src/cast/shape_info.cpp
    src/cast/shockwave3d_info.cpp
    src/cast/text_info.cpp
    src/cast/xmed_styled_text.cpp
    src/cast/xmed_text_parser.cpp
    src/chunks/bitmap_chunk.cpp
    src/chunks/cast_chunk.cpp
    src/chunks/cast_list_chunk.cpp
    src/chunks/cast_member_chunk.cpp
    src/chunks/config_chunk.cpp
    src/chunks/font_map_chunk.cpp
    src/chunks/frame_labels_chunk.cpp
    src/chunks/key_table_chunk.cpp
    src/chunks/media_chunk.cpp
    src/chunks/palette_chunk.cpp
    src/chunks/raw_chunk.cpp
    src/chunks/script_context_chunk.cpp
    src/chunks/script_chunk.cpp
    src/chunks/script_names_chunk.cpp
    src/chunks/score_chunk.cpp
    src/chunks/sound_chunk.cpp
    src/chunks/text_chunk.cpp
    src/format/afterburner_reader.cpp
    src/format/chunk_type.cpp
    src/format/moa_id.cpp
    src/format/script_format_utils.cpp
    src/font/bdf_parser.cpp
    src/font/bitmap_font.cpp
    src/font/pfr_bit_reader.cpp
    src/font/pfr1_font.cpp
    src/font/pfr1_ttf_converter.cpp
    src/font/ttf_bitmap_rasterizer.cpp
    src/fonts/font_data_decoder.cpp
    "${LIBRESHOCKWAVE_VOLTER_DATA_CPP}"
    "${LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP}"
    src/io/binary_reader.cpp
    src/lingo/datum.cpp
    src/lingo/lingo_value_parser.cpp
    src/lingo/opcode.cpp
    src/lingo/builtin/builtin_registry.cpp
    src/lingo/decompiler/lingo_decompiler.cpp
    src/lingo/decompiler/lingo_node.cpp
    src/lingo/decompiler/lingo_node_type_script.cpp
    src/lingo/decompiler/lingo_properties.cpp
    src/lingo/compiler/lexer.cpp
    src/lingo/compiler/parser.cpp
    src/lingo/compiler/parser_expr.cpp
    src/lingo/compiler/codegen.cpp
    src/lingo/vm/alert_hook_handler.cpp
    src/lingo/vm/debug_config.cpp
    src/lingo/vm/datum/datum_formatter.cpp
    src/lingo/vm/execution_context.cpp
    src/lingo/vm/lingo_vm.cpp
    src/lingo/vm/opcode_registry.cpp
    src/lingo/vm/property_id_mappings.cpp
    src/lingo/vm/scope.cpp
    src/lingo/vm/dispatch/list_method_dispatcher.cpp
    src/lingo/vm/dispatch/prop_list_method_dispatcher.cpp
    src/lingo/vm/dispatch/sound_channel_method_dispatcher.cpp
    src/lingo/vm/dispatch/string_method_dispatcher.cpp
    src/lingo/vm/parse/lingo_expression_parser.cpp
    src/lingo/vm/trace/console_trace_printer.cpp
    src/lingo/vm/trace/instruction_annotator.cpp
    src/lingo/vm/trace/tracing_helper.cpp
    src/lingo/vm/util/ancestor_chain_walker.cpp
    src/lingo/vm/util/string_chunk_utils.cpp
    src/lingo/xtra/multiuser_xtra.cpp
    src/lingo/xtra/xtra_manager.cpp
    src/lingo/xtra/xml_parser_xtra.cpp
    src/lookup/cast_member_lookup.cpp
    src/lookup/palette_resolver.cpp
    src/lookup/script_lookup.cpp
    src/moviesource/movie_source.cpp
    src/moviesource/director_movie_source.cpp
    src/moviesource/director_container_loader.cpp
    src/moviesource/director_record_converter.cpp
    src/moviesource/format_registry.cpp
    src/moviesource/zip_archive.cpp
    src/moviesource/records/script_asset.cpp
    src/moviesource/records/score_asset.cpp
    src/moviesource/records/movie_asset.cpp
    src/player/player_event.cpp
    src/player/player_state.cpp
    src/player/player.cpp
    src/player/bitmap_resolver.cpp
    src/player/cursor_manager.cpp
    src/player/input/editable_field_overlay.cpp
    src/player/input_handler.cpp
    src/player/movie_properties.cpp
    src/player/sprite_properties.cpp
    src/player/audio/queued_audio_backend.cpp
    src/player/audio/sound_manager.cpp
    src/player/audio/sound_queue.cpp
    src/player/media/queued_jpeg_decoder.cpp
    src/player/behavior/behavior_instance.cpp
    src/player/behavior/behavior_manager.cpp
    src/player/cast/cast_lib.cpp
    src/player/cast/cast_lib_manager.cpp
    src/player/cast/font_registry.cpp
    src/player/cast/mac_font_bundle.cpp
    src/player/cast/windows_font_bundle.cpp
    src/player/input/director_key_codes.cpp
    src/player/input/hit_tester.cpp
    src/player/input/input_event.cpp
    src/player/input/input_state.cpp
    src/player/net/net_manager.cpp
    src/player/net/net_task.cpp
    src/player/net/queued_net_provider.cpp
    src/player/debug/breakpoint.cpp
    src/player/debug/breakpoint_manager.cpp
    src/player/debug/debug_controller.cpp
    src/player/debug/expression_evaluator.cpp
    src/player/debug/lifecycle_diagnostics.cpp
    src/player/debug/watch_expression.cpp
    src/player/event/event_dispatcher.cpp
    src/player/frame/frame_context.cpp
    src/player/render/render_config.cpp
    src/player/render/render_type.cpp
    src/player/render/sprite_registry.cpp
    src/player/render/output/simple_text_renderer.cpp
    src/player/render/output/software_frame_renderer.cpp
    src/player/render/output/text_renderer.cpp
    src/player/render/pipeline/bitmap_cache.cpp
    src/player/render/pipeline/frame_render_pipeline.cpp
    src/player/render/pipeline/frame_snapshot.cpp
    src/player/render/pipeline/frame_render_pipeline_context.cpp
    src/player/render/pipeline/ink_processor.cpp
    src/player/render/pipeline/render_pipeline_trace.cpp
    src/player/render/pipeline/render_sprite.cpp
    src/player/render/pipeline/sprite_baker.cpp
    src/player/render/pipeline/stage_renderer.cpp
    src/player/score/score_behavior_ref.cpp
    src/player/score/score_navigator.cpp
    src/player/score/sprite_span.cpp
    src/player/sprite/sprite_state.cpp
    src/player/timeout/timeout_manager.cpp
    src/player/xtra/queued_multiuser_bridge.cpp
    src/player/xtra/socket_multiuser_bridge.cpp
    src/util/audio_codec_utils.cpp
    src/util/file_util.cpp
    src/util/string_utils.cpp
    src/w3d/w3d_entry.cpp
    src/w3d/w3d_entry_type.cpp
    src/w3d/w3d_material.cpp
    src/w3d/w3d_mesh_resource.cpp
    src/w3d/w3d_node.cpp
    src/w3d/w3d_resource_ref.cpp
    src/w3d/w3d_shape.cpp
    src/w3d/w3d_texture.cpp
)

add_library(LibreShockwave::libreshockwave ALIAS libreshockwave)

target_include_directories(libreshockwave
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(libreshockwave PUBLIC cxx_std_20)

add_executable(libreshockwave_probe
    apps/tools/director_probe.cpp
)
target_link_libraries(libreshockwave_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_asset_extractor
    apps/tools/asset_extractor.cpp
)
target_link_libraries(libreshockwave_asset_extractor PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_asset_extractor PRIVATE cxx_std_20)

find_package(JPEG QUIET)
if(JPEG_FOUND)
    target_link_libraries(libreshockwave_asset_extractor PRIVATE JPEG::JPEG)
    target_compile_definitions(libreshockwave_asset_extractor PRIVATE LIBRESHOCKWAVE_HAVE_JPEG=1)
endif()

add_executable(libreshockwave_render_probe
    apps/tools/render_probe.cpp
)
target_link_libraries(libreshockwave_render_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_render_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_wasm_bridge_probe
    apps/wasm/wasm_bridge.cpp
    apps/tools/wasm_bridge_probe.cpp
)
target_link_libraries(libreshockwave_wasm_bridge_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_wasm_bridge_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_multiuser_probe
    apps/tools/multiuser_flood_probe.cpp
)
target_link_libraries(libreshockwave_multiuser_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_multiuser_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_roundtrip_validate
    apps/tools/roundtrip_validate.cpp
)
target_link_libraries(libreshockwave_roundtrip_validate PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_roundtrip_validate PRIVATE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(libreshockwave PUBLIC Threads::Threads)

option(LIBRESHOCKWAVE_REQUIRE_ZLIB
    "Fail configuration when zlib is unavailable"
    OFF
)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(libreshockwave PUBLIC ZLIB::ZLIB)
    target_compile_definitions(libreshockwave PUBLIC LIBRESHOCKWAVE_HAVE_ZLIB=1)
    message(STATUS "LibreShockwave zlib support: enabled")
elseif(LIBRESHOCKWAVE_REQUIRE_ZLIB)
    message(FATAL_ERROR
        "zlib is required for this build but was not found. "
        "Install zlib development files or configure with a zlib CMake package."
    )
else()
    message(STATUS "LibreShockwave zlib support: disabled")
endif()

# SocketMultiuserBridge uses winsock2 on Windows.
if(WIN32)
    target_link_libraries(libreshockwave PRIVATE ws2_32)
endif()

if(BUILD_TESTING)
    add_executable(libreshockwave_tests
        tests/sdk_foundation_test.cpp
    )
    target_link_libraries(libreshockwave_tests PRIVATE LibreShockwave::libreshockwave)
    # The foundation tests use assertions for both verification and setup
    # expressions.  Keep them active even when the surrounding build is a
    # release build, otherwise NDEBUG removes calls from the test program and
    # changes the state being tested.
    target_compile_options(libreshockwave_tests PRIVATE -UNDEBUG)
    add_test(NAME libreshockwave_cpp_tests COMMAND libreshockwave_tests)
    add_test(NAME libreshockwave_wasm_bridge_probe COMMAND libreshockwave_wasm_bridge_probe)
endif()

if(EMSCRIPTEN)
    set(LIBRESHOCKWAVE_WASM_PROFILING "off" CACHE STRING "Emscripten profiling mode: off, functions, full, cpu, or memory")
    set_property(CACHE LIBRESHOCKWAVE_WASM_PROFILING PROPERTY STRINGS off functions full cpu memory)
    if(NOT LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "off" AND
       NOT LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "functions" AND
       NOT LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "full" AND
       NOT LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "cpu" AND
       NOT LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "memory")
        message(FATAL_ERROR "LIBRESHOCKWAVE_WASM_PROFILING must be one of: off, functions, full, cpu, memory")
    endif()

    target_compile_options(libreshockwave PUBLIC
        -O3
        -fwasm-exceptions
    )

    set(LIBRESHOCKWAVE_WASM_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/wasm-dist")
    set(LIBRESHOCKWAVE_WASM_EXPORTS
        "_malloc"
        "_free"
    )
    string(REPLACE ";" "','" LIBRESHOCKWAVE_WASM_EXPORTS_JOINED "${LIBRESHOCKWAVE_WASM_EXPORTS}")
    set(LIBRESHOCKWAVE_WASM_EXPORTS_FLAG "['${LIBRESHOCKWAVE_WASM_EXPORTS_JOINED}']")
    set(LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS)
    if(LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "functions")
        list(APPEND LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS "SHELL:--profiling-funcs")
    elseif(LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "full")
        list(APPEND LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS "SHELL:--profiling")
    elseif(LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "cpu")
        list(APPEND LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS "SHELL:--profiling-funcs" "SHELL:--cpuprofiler")
    elseif(LIBRESHOCKWAVE_WASM_PROFILING STREQUAL "memory")
        list(APPEND LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS "SHELL:--profiling-funcs" "SHELL:--memoryprofiler")
    endif()

    add_executable(libreshockwave_cpp_wasm
        apps/wasm/wasm_bridge.cpp
        ../debugger/cpp/debugger_bridge.cpp
    )
    target_link_libraries(libreshockwave_cpp_wasm PRIVATE LibreShockwave::libreshockwave)
    target_compile_features(libreshockwave_cpp_wasm PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_cpp_wasm PRIVATE
        -O3
        -fwasm-exceptions
    )
    target_link_options(libreshockwave_cpp_wasm PRIVATE
        "SHELL:-O3"
        "SHELL:-fwasm-exceptions"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME=createLibreShockwaveCppWasm"
        "SHELL:-s ENVIRONMENT=worker"
        "SHELL:-sALLOW_MEMORY_GROWTH"
        "SHELL:-s STACK_SIZE=8388608"
        "SHELL:-s EXIT_RUNTIME=0"
        "SHELL:-s EXPORTED_FUNCTIONS=${LIBRESHOCKWAVE_WASM_EXPORTS_FLAG}"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8']"
        ${LIBRESHOCKWAVE_WASM_PROFILING_LINK_OPTIONS}
    )
    set_target_properties(libreshockwave_cpp_wasm PROPERTIES
        OUTPUT_NAME "libreshockwave-cpp-wasm"
        RUNTIME_OUTPUT_DIRECTORY "${LIBRESHOCKWAVE_WASM_DIST_DIR}"
    )

    add_custom_target(libreshockwave_cpp_wasm_dist
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIBRESHOCKWAVE_WASM_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/../web"
                "${LIBRESHOCKWAVE_WASM_DIST_DIR}"
        DEPENDS libreshockwave_cpp_wasm
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Qt6 Debugger application
# ---------------------------------------------------------------------------

find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(LIBRESHOCKWAVE_QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 QUIET COMPONENTS Widgets)
    if(Qt5_FOUND)
        set(LIBRESHOCKWAVE_QT_WIDGETS_TARGET Qt5::Widgets)
    endif()
endif()

if(DEFINED LIBRESHOCKWAVE_QT_WIDGETS_TARGET)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../debugger/cpp" libreshockwave_debugger_app)
else()
    message(STATUS "Qt Widgets not found; libreshockwave_debugger target disabled. Install Qt6 Widgets or Qt5 Widgets development packages to build it.")
endif()
