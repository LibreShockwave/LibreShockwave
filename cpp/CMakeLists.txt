set(LIBRESHOCKWAVE_VOLTER_DATA_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/src/fonts/volter/VolterData.cpp")
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

set(LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/src/fonts/PlatformFontData.cpp")
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
    src/DirectorFile.cpp
    src/W3DFile.cpp
    src/audio/SoundConverter.cpp
    src/bitmap/Bitmap.cpp
    src/bitmap/BitmapColorizer.cpp
    src/bitmap/BitmapDecoder.cpp
    src/bitmap/ColorRef.cpp
    src/bitmap/Drawing.cpp
    src/bitmap/Palette.cpp
    src/bitmap/PngDecoder.cpp
    src/cast/BitmapInfo.cpp
    src/cast/CastMember.cpp
    src/cast/FilmLoopInfo.cpp
    src/cast/MemberType.cpp
    src/cast/ScriptType.cpp
    src/cast/ShapeInfo.cpp
    src/cast/Shockwave3DInfo.cpp
    src/cast/TextInfo.cpp
    src/cast/XmedStyledText.cpp
    src/cast/XmedTextParser.cpp
    src/chunks/BitmapChunk.cpp
    src/chunks/CastChunk.cpp
    src/chunks/CastListChunk.cpp
    src/chunks/CastMemberChunk.cpp
    src/chunks/ConfigChunk.cpp
    src/chunks/FontMapChunk.cpp
    src/chunks/FrameLabelsChunk.cpp
    src/chunks/KeyTableChunk.cpp
    src/chunks/MediaChunk.cpp
    src/chunks/PaletteChunk.cpp
    src/chunks/RawChunk.cpp
    src/chunks/ScriptContextChunk.cpp
    src/chunks/ScriptChunk.cpp
    src/chunks/ScriptNamesChunk.cpp
    src/chunks/ScoreChunk.cpp
    src/chunks/SoundChunk.cpp
    src/chunks/TextChunk.cpp
    src/format/AfterburnerReader.cpp
    src/format/ChunkType.cpp
    src/format/MoaID.cpp
    src/format/ScriptFormatUtils.cpp
    src/font/BdfParser.cpp
    src/font/BitmapFont.cpp
    src/font/PfrBitReader.cpp
    src/font/Pfr1Font.cpp
    src/font/Pfr1TtfConverter.cpp
    src/font/TtfBitmapRasterizer.cpp
    src/fonts/FontDataDecoder.cpp
    "${LIBRESHOCKWAVE_VOLTER_DATA_CPP}"
    "${LIBRESHOCKWAVE_PLATFORM_FONT_DATA_CPP}"
    src/io/BinaryReader.cpp
    src/lingo/Datum.cpp
    src/lingo/LingoValueParser.cpp
    src/lingo/Opcode.cpp
    src/lingo/builtin/BuiltinRegistry.cpp
    src/lingo/decompiler/LingoDecompiler.cpp
    src/lingo/decompiler/LingoNode.cpp
    src/lingo/decompiler/LingoProperties.cpp
    src/lingo/vm/AlertHookHandler.cpp
    src/lingo/vm/DebugConfig.cpp
    src/lingo/vm/datum/DatumFormatter.cpp
    src/lingo/vm/ExecutionContext.cpp
    src/lingo/vm/LingoVM.cpp
    src/lingo/vm/OpcodeRegistry.cpp
    src/lingo/vm/PropertyIdMappings.cpp
    src/lingo/vm/Scope.cpp
    src/lingo/vm/dispatch/ListMethodDispatcher.cpp
    src/lingo/vm/dispatch/PropListMethodDispatcher.cpp
    src/lingo/vm/dispatch/SoundChannelMethodDispatcher.cpp
    src/lingo/vm/dispatch/StringMethodDispatcher.cpp
    src/lingo/vm/parse/LingoExpressionParser.cpp
    src/lingo/vm/trace/ConsoleTracePrinter.cpp
    src/lingo/vm/trace/InstructionAnnotator.cpp
    src/lingo/vm/trace/TracingHelper.cpp
    src/lingo/vm/util/AncestorChainWalker.cpp
    src/lingo/vm/util/StringChunkUtils.cpp
    src/lingo/xtra/MultiuserXtra.cpp
    src/lingo/xtra/XtraManager.cpp
    src/lingo/xtra/XmlParserXtra.cpp
    src/lookup/CastMemberLookup.cpp
    src/lookup/PaletteResolver.cpp
    src/lookup/ScriptLookup.cpp
    src/player/PlayerEvent.cpp
    src/player/PlayerState.cpp
    src/player/Player.cpp
    src/player/BitmapResolver.cpp
    src/player/CursorManager.cpp
    src/player/InputHandler.cpp
    src/player/MovieProperties.cpp
    src/player/SpriteProperties.cpp
    src/player/audio/QueuedAudioBackend.cpp
    src/player/audio/SoundManager.cpp
    src/player/media/QueuedJpegDecoder.cpp
    src/player/behavior/BehaviorInstance.cpp
    src/player/behavior/BehaviorManager.cpp
    src/player/cast/CastLib.cpp
    src/player/cast/CastLibManager.cpp
    src/player/cast/FontRegistry.cpp
    src/player/cast/MacFontBundle.cpp
    src/player/cast/WindowsFontBundle.cpp
    src/player/input/DirectorKeyCodes.cpp
    src/player/input/HitTester.cpp
    src/player/input/InputEvent.cpp
    src/player/input/InputState.cpp
    src/player/net/NetManager.cpp
    src/player/net/NetTask.cpp
    src/player/net/QueuedNetProvider.cpp
    src/player/debug/Breakpoint.cpp
    src/player/debug/BreakpointManager.cpp
    src/player/debug/DebugController.cpp
    src/player/debug/ExpressionEvaluator.cpp
    src/player/debug/LifecycleDiagnostics.cpp
    src/player/debug/WatchExpression.cpp
    src/player/event/EventDispatcher.cpp
    src/player/frame/FrameContext.cpp
    src/player/render/RenderConfig.cpp
    src/player/render/RenderType.cpp
    src/player/render/SpriteRegistry.cpp
    src/player/render/output/SimpleTextRenderer.cpp
    src/player/render/output/SoftwareFrameRenderer.cpp
    src/player/render/output/TextRenderer.cpp
    src/player/render/pipeline/BitmapCache.cpp
    src/player/render/pipeline/FrameRenderPipeline.cpp
    src/player/render/pipeline/FrameSnapshot.cpp
    src/player/render/pipeline/FrameRenderPipelineContext.cpp
    src/player/render/pipeline/InkProcessor.cpp
    src/player/render/pipeline/RenderPipelineTrace.cpp
    src/player/render/pipeline/RenderSprite.cpp
    src/player/render/pipeline/SpriteBaker.cpp
    src/player/render/pipeline/StageRenderer.cpp
    src/player/score/ScoreBehaviorRef.cpp
    src/player/score/ScoreNavigator.cpp
    src/player/score/SpriteSpan.cpp
    src/player/sprite/SpriteState.cpp
    src/player/timeout/TimeoutManager.cpp
    src/player/xtra/QueuedMultiuserBridge.cpp
    src/player/xtra/SocketMultiuserBridge.cpp
    src/util/AudioCodecUtils.cpp
    src/util/FileUtil.cpp
    src/util/StringUtils.cpp
    src/w3d/W3DEntry.cpp
    src/w3d/W3DEntryType.cpp
    src/w3d/W3DMaterial.cpp
    src/w3d/W3DMeshResource.cpp
    src/w3d/W3DNode.cpp
    src/w3d/W3DResourceRef.cpp
    src/w3d/W3DShape.cpp
    src/w3d/W3DTexture.cpp
)

add_library(LibreShockwave::libreshockwave ALIAS libreshockwave)

target_include_directories(libreshockwave
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(libreshockwave PUBLIC cxx_std_20)

add_executable(libreshockwave_probe
    apps/tools/DirectorProbe.cpp
)
target_link_libraries(libreshockwave_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_render_probe
    apps/tools/RenderProbe.cpp
)
target_link_libraries(libreshockwave_render_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_render_probe PRIVATE cxx_std_20)

add_executable(libreshockwave_wasm_bridge_probe
    apps/wasm/WasmBridge.cpp
    apps/tools/WasmBridgeProbe.cpp
)
target_link_libraries(libreshockwave_wasm_bridge_probe PRIVATE LibreShockwave::libreshockwave)
target_compile_features(libreshockwave_wasm_bridge_probe PRIVATE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(libreshockwave PUBLIC Threads::Threads)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(libreshockwave PUBLIC ZLIB::ZLIB)
    target_compile_definitions(libreshockwave PUBLIC LIBRESHOCKWAVE_HAVE_ZLIB=1)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GTK4 QUIET IMPORTED_TARGET gtk4)
endif()
find_package(CURL QUIET)

if(GTK4_FOUND AND CURL_FOUND)
    add_executable(libreshockwave_player
        apps/player/NativePlayer.cpp
    )
    target_link_libraries(libreshockwave_player
        PRIVATE
            LibreShockwave::libreshockwave
            PkgConfig::GTK4
            CURL::libcurl
    )
    target_compile_features(libreshockwave_player PRIVATE cxx_std_20)
else()
    message(STATUS "GTK4 or libcurl not found; libreshockwave_player target disabled. Install gtk4-devel and libcurl-devel to build it.")
endif()

if(BUILD_TESTING)
    add_executable(libreshockwave_tests
        tests/sdk_foundation_test.cpp
    )
    target_link_libraries(libreshockwave_tests PRIVATE LibreShockwave::libreshockwave)
    add_test(NAME libreshockwave_cpp_tests COMMAND libreshockwave_tests)
    add_test(NAME libreshockwave_wasm_bridge_probe COMMAND libreshockwave_wasm_bridge_probe)
endif()

if(EMSCRIPTEN)
    target_compile_options(libreshockwave PUBLIC -fexceptions)

    set(LIBRESHOCKWAVE_WASM_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/wasm-dist")
    set(LIBRESHOCKWAVE_WASM_EXPORTS
        "_malloc"
        "_free"
        "_lsw_create"
        "_lsw_destroy"
        "_lsw_load_movie"
        "_lsw_set_external_params"
        "_lsw_set_preload_casts"
        "_lsw_set_debug_playback_enabled"
        "_lsw_set_slow_handler_warning_ms"
        "_lsw_set_tempo_override"
        "_lsw_tempo"
        "_lsw_base_tempo"
        "_lsw_play"
        "_lsw_pause"
        "_lsw_stop"
        "_lsw_tick"
        "_lsw_render_frame"
        "_lsw_frame_pixels"
        "_lsw_frame_byte_length"
        "_lsw_frame_width"
        "_lsw_frame_height"
        "_lsw_frame_info_json"
        "_lsw_last_error"
        "_lsw_poll_debug_messages"
        "_lsw_drain_debug_messages"
        "_lsw_poll_fetch_requests"
        "_lsw_drain_fetch_requests"
        "_lsw_poll_movie_navigation_requests"
        "_lsw_movie_navigation_complete"
        "_lsw_fetch_complete"
        "_lsw_fetch_error"
        "_lsw_poll_multiuser_requests"
        "_lsw_drain_multiuser_requests"
        "_lsw_multiuser_connected"
        "_lsw_multiuser_disconnected"
        "_lsw_multiuser_error"
        "_lsw_multiuser_message_bytes"
        "_lsw_director_key_from_browser"
        "_lsw_mouse_move"
        "_lsw_mouse_down"
        "_lsw_mouse_up"
        "_lsw_blur"
        "_lsw_key_down"
        "_lsw_key_up"
        "_lsw_paste_text"
        "_lsw_selected_text"
        "_lsw_select_all"
        "_lsw_cut_selected_text"
    )
    string(REPLACE ";" "','" LIBRESHOCKWAVE_WASM_EXPORTS_JOINED "${LIBRESHOCKWAVE_WASM_EXPORTS}")
    set(LIBRESHOCKWAVE_WASM_EXPORTS_FLAG "['${LIBRESHOCKWAVE_WASM_EXPORTS_JOINED}']")

    add_executable(libreshockwave_cpp_wasm
        apps/wasm/WasmBridge.cpp
    )
    target_link_libraries(libreshockwave_cpp_wasm PRIVATE LibreShockwave::libreshockwave)
    target_compile_features(libreshockwave_cpp_wasm PRIVATE cxx_std_20)
    target_compile_options(libreshockwave_cpp_wasm PRIVATE -fexceptions)
    target_link_options(libreshockwave_cpp_wasm PRIVATE
        "SHELL:-fexceptions"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME=createLibreShockwaveCppWasm"
        "SHELL:-s ENVIRONMENT=worker"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s STACK_SIZE=8388608"
        "SHELL:-s EXIT_RUNTIME=0"
        "SHELL:-s NO_DISABLE_EXCEPTION_CATCHING=1"
        "SHELL:-s EXPORTED_FUNCTIONS=${LIBRESHOCKWAVE_WASM_EXPORTS_FLAG}"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8']"
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
