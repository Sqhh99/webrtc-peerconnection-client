cmake_minimum_required(VERSION 3.21)

project(peerconnection_client LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "peerconnection_client currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

include(cmake/FetchWebRTC.cmake)
ensure_webrtc_prebuilt()

add_executable(peerconnection_client
    src/main.cc
    src/sdl_app.cc
    src/call_coordinator.cc
    src/webrtcengine.cc
    src/defaults.cc
    src/videorenderer.cc
    src/signalclient.cc
    src/callmanager.cc
    src/test_impl.cc
    test/platform_video_capturer.cc
    test/vcm_capturer.cc
    test/test_video_capturer.cc
    test/frame_generator.cc
    test/frame_generator_capturer.cc
    test/frame_utils.cc
    include/app_config.h
    include/call_coordinator.h
    include/callmanager.h
    include/defaults.h
    include/icall_observer.h
    include/sdl_app.h
    include/signal_types.h
    include/signalclient.h
    include/videorenderer.h
    include/webrtcengine.h
)

target_include_directories(
    peerconnection_client
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
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
        SDL_MAIN_HANDLED
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
        Boost::system
        imgui::imgui
        nlohmann_json::nlohmann_json
        SDL3::SDL3
        ${SYSTEM_LIBRARIES}
)

set_target_properties(peerconnection_client PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)
