include_guard(GLOBAL)

set(WEBRTC_VERSION "m146.7680.1.0" CACHE STRING "shiguredo-webrtc-build release tag")
set(WEBRTC_PLATFORM "windows_x86_64" CACHE STRING "shiguredo-webrtc-build platform asset name")
set(
    WEBRTC_DOWNLOAD_DIR
    "${PROJECT_SOURCE_DIR}/third_party/_downloads"
    CACHE PATH
    "Directory used to store downloaded WebRTC archives"
)
set(
    WEBRTC_EXTRACT_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/webrtc"
    CACHE PATH
    "Root directory used to extract WebRTC archives"
)
option(WEBRTC_FORCE_DOWNLOAD "Force re-download and re-extract the WebRTC archive" OFF)

function(_webrtc_find_include_dir out_var search_root)
    file(
        GLOB_RECURSE _webrtc_header_candidates
        LIST_DIRECTORIES FALSE
        "${search_root}/include/api/peer_connection_interface.h"
        "${search_root}/*/include/api/peer_connection_interface.h"
    )
    list(LENGTH _webrtc_header_candidates _webrtc_header_count)
    if(_webrtc_header_count EQUAL 0)
        message(FATAL_ERROR "Failed to locate WebRTC headers under: ${search_root}")
    endif()

    list(GET _webrtc_header_candidates 0 _webrtc_header_path)
    get_filename_component(_webrtc_api_dir "${_webrtc_header_path}" DIRECTORY)
    get_filename_component(_webrtc_include_dir "${_webrtc_api_dir}" DIRECTORY)
    set(${out_var} "${_webrtc_include_dir}" PARENT_SCOPE)
endfunction()

function(_webrtc_find_library_path out_var search_root)
    file(
        GLOB_RECURSE _webrtc_library_candidates
        LIST_DIRECTORIES FALSE
        "${search_root}/lib/webrtc.lib"
        "${search_root}/*/lib/webrtc.lib"
    )
    list(LENGTH _webrtc_library_candidates _webrtc_library_count)
    if(_webrtc_library_count EQUAL 0)
        message(FATAL_ERROR "Failed to locate webrtc.lib under: ${search_root}")
    endif()

    list(GET _webrtc_library_candidates 0 _webrtc_library_path)
    set(${out_var} "${_webrtc_library_path}" PARENT_SCOPE)
endfunction()

function(ensure_webrtc_prebuilt)
    if(NOT WIN32)
        message(FATAL_ERROR "The current build is configured only for Windows WebRTC prebuilts.")
    endif()

    if(WEBRTC_PLATFORM MATCHES "^windows_")
        set(_webrtc_archive_name "webrtc.${WEBRTC_PLATFORM}.zip")
    else()
        set(_webrtc_archive_name "webrtc.${WEBRTC_PLATFORM}.tar.gz")
    endif()

    set(
        _webrtc_download_url
        "https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/${WEBRTC_VERSION}/${_webrtc_archive_name}"
    )
    set(_webrtc_archive_path "${WEBRTC_DOWNLOAD_DIR}/${WEBRTC_VERSION}-${_webrtc_archive_name}")
    set(_webrtc_extract_dir "${WEBRTC_EXTRACT_ROOT}/${WEBRTC_VERSION}/${WEBRTC_PLATFORM}")
    set(_webrtc_extract_stamp "${_webrtc_extract_dir}/.extract-stamp")

    if(WEBRTC_FORCE_DOWNLOAD)
        file(REMOVE "${_webrtc_archive_path}")
        file(REMOVE_RECURSE "${_webrtc_extract_dir}")
    endif()

    file(MAKE_DIRECTORY "${WEBRTC_DOWNLOAD_DIR}")

    if(NOT EXISTS "${_webrtc_archive_path}")
        message(STATUS "Downloading WebRTC prebuilt: ${_webrtc_download_url}")
        file(
            DOWNLOAD
            "${_webrtc_download_url}"
            "${_webrtc_archive_path}"
            SHOW_PROGRESS
            STATUS _webrtc_download_status
            TLS_VERIFY ON
        )
        list(GET _webrtc_download_status 0 _webrtc_download_code)
        list(GET _webrtc_download_status 1 _webrtc_download_message)
        if(NOT _webrtc_download_code EQUAL 0)
            file(REMOVE "${_webrtc_archive_path}")
            message(
                FATAL_ERROR
                "Failed to download WebRTC archive from ${_webrtc_download_url}: ${_webrtc_download_message}"
            )
        endif()
    endif()

    if(NOT EXISTS "${_webrtc_extract_stamp}")
        file(REMOVE_RECURSE "${_webrtc_extract_dir}")
        file(MAKE_DIRECTORY "${_webrtc_extract_dir}")

        message(STATUS "Extracting WebRTC archive: ${_webrtc_archive_path}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${_webrtc_archive_path}"
            WORKING_DIRECTORY "${_webrtc_extract_dir}"
            RESULT_VARIABLE _webrtc_extract_result
        )
        if(NOT _webrtc_extract_result EQUAL 0)
            file(REMOVE_RECURSE "${_webrtc_extract_dir}")
            message(FATAL_ERROR "Failed to extract WebRTC archive: ${_webrtc_archive_path}")
        endif()

        file(WRITE "${_webrtc_extract_stamp}" "${WEBRTC_VERSION}\n${WEBRTC_PLATFORM}\n")
    endif()

    _webrtc_find_include_dir(_webrtc_include_dir "${_webrtc_extract_dir}")
    _webrtc_find_library_path(_webrtc_library_path "${_webrtc_extract_dir}")
    get_filename_component(_webrtc_package_root "${_webrtc_include_dir}" DIRECTORY)

    set(_webrtc_interface_include_dirs "${_webrtc_include_dir}")

    foreach(
        _webrtc_extra_include
        IN ITEMS
            "${_webrtc_include_dir}/third_party/abseil-cpp"
            "${_webrtc_include_dir}/third_party/libyuv/include"
            "${_webrtc_include_dir}/third_party/jsoncpp/source/include"
            "${_webrtc_include_dir}/third_party/jsoncpp/generated"
    )
        if(EXISTS "${_webrtc_extra_include}")
            list(APPEND _webrtc_interface_include_dirs "${_webrtc_extra_include}")
        endif()
    endforeach()

    set(WEBRTC_EXTRACT_DIR "${_webrtc_extract_dir}" CACHE PATH "Resolved WebRTC extraction directory" FORCE)
    set(WEBRTC_INCLUDE_DIR "${_webrtc_include_dir}" CACHE PATH "Resolved WebRTC include directory" FORCE)
    set(WEBRTC_LIBRARY_PATH "${_webrtc_library_path}" CACHE FILEPATH "Resolved WebRTC library path" FORCE)
    set(WEBRTC_PACKAGE_ROOT "${_webrtc_package_root}" CACHE PATH "Resolved WebRTC package root directory" FORCE)

    if(NOT TARGET webrtc_prebuilt)
        add_library(webrtc_prebuilt STATIC IMPORTED GLOBAL)
    endif()

    set_target_properties(
        webrtc_prebuilt
        PROPERTIES
            IMPORTED_LOCATION "${WEBRTC_LIBRARY_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${_webrtc_interface_include_dirs}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_webrtc_interface_include_dirs}"
    )
endfunction()
