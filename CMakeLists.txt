cmake_minimum_required(VERSION 3.21)

project(peerconnection_client LANGUAGES CXX)
include(CTest)

if(NOT WIN32)
    message(FATAL_ERROR "peerconnection_client currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)

set(SLINT_ROOT "${CMAKE_SOURCE_DIR}/third_party/Slint-cpp 1.15.1")
set(SLINT_INCLUDE_DIR "${SLINT_ROOT}/include")
set(SLINT_RUNTIME_DLL "${SLINT_ROOT}/lib/slint_cpp.dll")
set(SLINT_IMPORT_LIB "${SLINT_ROOT}/lib/slint_cpp.dll.lib")

if(NOT EXISTS "${SLINT_INCLUDE_DIR}/slint/slint-interpreter.h")
    message(FATAL_ERROR "Slint headers not found under ${SLINT_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${SLINT_IMPORT_LIB}")
    message(FATAL_ERROR "Slint import library not found: ${SLINT_IMPORT_LIB}")
endif()

add_library(slint_cpp SHARED IMPORTED GLOBAL)
set_target_properties(slint_cpp PROPERTIES
    IMPORTED_IMPLIB "${SLINT_IMPORT_LIB}"
    IMPORTED_LOCATION "${SLINT_RUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${SLINT_INCLUDE_DIR};${SLINT_INCLUDE_DIR}/slint"
)

include(cmake/FetchWebRTC.cmake)
ensure_webrtc_prebuilt()
include(cmake/FetchFFmpeg.cmake)
ensure_ffmpeg_prebuilt()

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/ui" PEERCONNECTION_UI_DIR)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/ui_paths.h.in"
    "${CMAKE_BINARY_DIR}/generated/ui_paths.h"
    @ONLY
)

add_executable(peerconnection_client
    WIN32
    src/main.cc
    src/slint_app.cc
    src/native_video_host.cc
    ui/AppWindow.slint
    src/call_coordinator.cc
    src/call_coordinator_ports.cc
    src/webrtcengine.cc
    src/webrtcengine_peer_connection.cc
    src/webrtcengine_media_pipeline.cc
    src/webrtcengine_telemetry.cc
    src/defaults.cc
    src/videorenderer.cc
    src/signalclient.cc
    src/signaling_codec.cc
    src/signal_message_dispatcher.cc
    src/callmanager.cc
    src/ice_disconnect_watchdog.cc
    src/ffmpeg_file_source.cc
    src/switchable_audio_input_win.cc
    src/test_impl.cc
    test/platform_video_capturer.cc
    test/vcm_capturer.cc
    test/test_video_capturer.cc
    test/frame_generator.cc
    test/frame_generator_capturer.cc
    test/frame_utils.cc
    include/app_config.h
    include/call_coordinator.h
    include/call_coordinator_ports.h
    include/callmanager.h
    include/defaults.h
    include/call_signaling_transport.h
    include/ice_disconnect_watchdog.h
    include/icall_observer.h
    include/local_media_source.h
    include/managed_video_track_source.h
    include/ffmpeg_file_source.h
    include/slint_app.h
    include/native_video_host.h
    include/signal_types.h
    include/signalclient.h
    include/signal_message_dispatcher.h
    include/signaling_codec.h
    include/switchable_audio_input_win.h
    include/videorenderer.h
    include/webrtcengine.h
    src/webrtcengine_internal.h
)

target_include_directories(
    peerconnection_client
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}"
        "${SLINT_INCLUDE_DIR}"
        "${SLINT_INCLUDE_DIR}/slint"
)

if(MSVC)
    target_compile_options(
        peerconnection_client
        PRIVATE
            /Zc:__cplusplus
            /EHsc
    )
endif()

target_compile_definitions(
    peerconnection_client
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WEBRTC_WIN
        _CRT_SECURE_NO_WARNINGS
        _SCL_SECURE_NO_WARNINGS
        WEBRTC_LITTLE_ENDIAN
        _WIN32_WINNT=0x0A00
        _HAS_ITERATOR_DEBUGGING=0
        _ITERATOR_DEBUG_LEVEL=0
        WEBRTC_LIBRARY_IMPL
        WEBRTC_NON_STATIC_TRACE_EVENT_HANDLERS=1
        UNICODE
        _UNICODE
)

set(SYSTEM_LIBRARIES
    ws2_32
    secur32
    winmm
    iphlpapi
    advapi32
    user32
    crypt32
    dmoguids
    msdmo
    strmiids
    wmcodecdspuuid
    comctl32
    gdi32
    comdlg32
    ole32
    shell32
    d3d11
    dxgi
    dxguid
)

target_link_libraries(
    peerconnection_client
    PRIVATE
        webrtc_prebuilt
        ffmpeg_prebuilt
        Boost::system
        nlohmann_json::nlohmann_json
        slint_cpp
        ${SYSTEM_LIBRARIES}
)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()

foreach(FFMPEG_RUNTIME_DLL IN LISTS FFMPEG_RUNTIME_DLLS)
    add_custom_command(
        TARGET peerconnection_client
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:peerconnection_client>"
    )
endforeach()

add_custom_command(
    TARGET peerconnection_client
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SLINT_RUNTIME_DLL}"
        "$<TARGET_FILE_DIR:peerconnection_client>"
)

set_target_properties(peerconnection_client PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)
