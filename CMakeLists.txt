# CMake 最低版本要求
cmake_minimum_required(VERSION 3.15)

# 项目名称
project(peerconnection_client)

if(NOT WIN32)
    message(FATAL_ERROR "peerconnection_client currently supports Windows only.")
endif()

# Qt 配置
if(NOT CMAKE_PREFIX_PATH AND EXISTS "d:/Qt/6.10.0/msvc2022_64")
    list(APPEND CMAKE_PREFIX_PATH "d:/Qt/6.10.0/msvc2022_64")
endif()
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt6 包
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets)

# 设置 C++ 标准为 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # 强制使用静态运行时库 /MT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

include(cmake/FetchWebRTC.cmake)
ensure_webrtc_prebuilt()

# --- 生成目标 (Windows GUI 应用) ---
add_executable(peerconnection_client
    src/main.cc
    # 新的重构文件
    src/call_coordinator.cc
    src/video_call_window.cc
    # 其他源文件
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
    # 新的重构文件
    include/icall_observer.h
    include/call_coordinator.h
    include/video_call_window.h
    # 其他头文件
    include/videorenderer.h
    include/signalclient.h
    include/callmanager.h
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

# Windows 系统库
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
)

# 链接所有库
target_link_libraries(peerconnection_client
    PRIVATE
    webrtc_prebuilt
    ${SYSTEM_LIBRARIES}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
)

# # 设置链接选项
# set_target_properties(peerconnection_client PROPERTIES
#     LINK_FLAGS "/SUBSYSTEM:WINDOWS"
# )

set_target_properties(peerconnection_client PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)
