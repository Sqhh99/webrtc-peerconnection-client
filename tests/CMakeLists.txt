add_executable(peerconnection_tests
    test_main.cc
    unit/signaling_codec_test.cc
    unit/callmanager_test.cc
    unit/callmanager_race_test.cc
    unit/ice_disconnect_watchdog_test.cc
    unit/call_coordinator_test.cc
    unit/signalclient_dispatch_test.cc
    integration/call_flow_integration_test.cc
    ${CMAKE_SOURCE_DIR}/src/call_coordinator.cc
    ${CMAKE_SOURCE_DIR}/src/signaling_codec.cc
    ${CMAKE_SOURCE_DIR}/src/signal_message_dispatcher.cc
    ${CMAKE_SOURCE_DIR}/src/callmanager.cc
    ${CMAKE_SOURCE_DIR}/src/ice_disconnect_watchdog.cc
)

target_include_directories(
    peerconnection_tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/tests"
)

if(MSVC)
    target_compile_options(
        peerconnection_tests
        PRIVATE
            /Zc:__cplusplus
            /EHsc
    )
endif()

target_compile_definitions(
    peerconnection_tests
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

target_link_libraries(
    peerconnection_tests
    PRIVATE
        GTest::gtest
        nlohmann_json::nlohmann_json
        webrtc_prebuilt
        Boost::system
        ${SYSTEM_LIBRARIES}
)

include(GoogleTest)
gtest_discover_tests(
    peerconnection_tests
    DISCOVERY_MODE PRE_TEST
)
