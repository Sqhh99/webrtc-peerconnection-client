include_guard(GLOBAL)

set(
    FFMPEG_RELEASE_TAG
    "autobuild-2026-03-15-12-59"
    CACHE STRING
    "BtbN FFmpeg release tag"
)
set(
    FFMPEG_PACKAGE_VERSION
    "n7.1.3-43-g5a1f107b4c"
    CACHE STRING
    "Pinned FFmpeg package version from BtbN"
)
set(
    FFMPEG_PACKAGE_TARGET
    "win64-lgpl-shared-7.1"
    CACHE STRING
    "Pinned FFmpeg package target from BtbN"
)
set(
    FFMPEG_DOWNLOAD_DIR
    "${PROJECT_SOURCE_DIR}/third_party/_downloads"
    CACHE PATH
    "Directory used to store downloaded FFmpeg archives"
)
set(
    FFMPEG_EXTRACT_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/ffmpeg"
    CACHE PATH
    "Root directory used to extract FFmpeg archives"
)
option(FFMPEG_FORCE_DOWNLOAD "Force re-download and re-extract the FFmpeg archive" OFF)

function(_ffmpeg_find_root out_var search_root)
    file(
        GLOB_RECURSE _ffmpeg_header_candidates
        LIST_DIRECTORIES FALSE
        "${search_root}/include/libavcodec/avcodec.h"
        "${search_root}/*/include/libavcodec/avcodec.h"
    )
    list(LENGTH _ffmpeg_header_candidates _ffmpeg_header_count)
    if(_ffmpeg_header_count EQUAL 0)
        message(FATAL_ERROR "Failed to locate FFmpeg headers under: ${search_root}")
    endif()

    list(GET _ffmpeg_header_candidates 0 _ffmpeg_header_path)
    get_filename_component(_ffmpeg_include_dir "${_ffmpeg_header_path}" DIRECTORY)
    get_filename_component(_ffmpeg_include_root "${_ffmpeg_include_dir}" DIRECTORY)
    get_filename_component(_ffmpeg_root "${_ffmpeg_include_root}" DIRECTORY)
    set(${out_var} "${_ffmpeg_root}" PARENT_SCOPE)
endfunction()

function(ensure_ffmpeg_prebuilt)
    if(NOT WIN32)
        message(FATAL_ERROR "The current build is configured only for Windows FFmpeg prebuilts.")
    endif()

    set(_ffmpeg_archive_name "ffmpeg-${FFMPEG_PACKAGE_VERSION}-${FFMPEG_PACKAGE_TARGET}.zip")
    set(
        _ffmpeg_download_url
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/${FFMPEG_RELEASE_TAG}/${_ffmpeg_archive_name}"
    )
    set(_ffmpeg_archive_path "${FFMPEG_DOWNLOAD_DIR}/${_ffmpeg_archive_name}")
    set(_ffmpeg_extract_dir "${FFMPEG_EXTRACT_ROOT}/${FFMPEG_PACKAGE_VERSION}/${FFMPEG_PACKAGE_TARGET}")
    set(_ffmpeg_extract_stamp "${_ffmpeg_extract_dir}/.extract-stamp")

    if(FFMPEG_FORCE_DOWNLOAD)
        file(REMOVE "${_ffmpeg_archive_path}")
        file(REMOVE_RECURSE "${_ffmpeg_extract_dir}")
    endif()

    file(MAKE_DIRECTORY "${FFMPEG_DOWNLOAD_DIR}")

    if(NOT EXISTS "${_ffmpeg_archive_path}")
        message(STATUS "Downloading FFmpeg prebuilt: ${_ffmpeg_download_url}")
        file(
            DOWNLOAD
            "${_ffmpeg_download_url}"
            "${_ffmpeg_archive_path}"
            SHOW_PROGRESS
            STATUS _ffmpeg_download_status
            TLS_VERIFY ON
        )
        list(GET _ffmpeg_download_status 0 _ffmpeg_download_code)
        list(GET _ffmpeg_download_status 1 _ffmpeg_download_message)
        if(NOT _ffmpeg_download_code EQUAL 0)
            file(REMOVE "${_ffmpeg_archive_path}")
            message(
                FATAL_ERROR
                "Failed to download FFmpeg archive from ${_ffmpeg_download_url}: ${_ffmpeg_download_message}"
            )
        endif()
    endif()

    if(NOT EXISTS "${_ffmpeg_extract_stamp}")
        file(REMOVE_RECURSE "${_ffmpeg_extract_dir}")
        file(MAKE_DIRECTORY "${_ffmpeg_extract_dir}")

        message(STATUS "Extracting FFmpeg archive: ${_ffmpeg_archive_path}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${_ffmpeg_archive_path}"
            WORKING_DIRECTORY "${_ffmpeg_extract_dir}"
            RESULT_VARIABLE _ffmpeg_extract_result
        )
        if(NOT _ffmpeg_extract_result EQUAL 0)
            file(REMOVE_RECURSE "${_ffmpeg_extract_dir}")
            message(FATAL_ERROR "Failed to extract FFmpeg archive: ${_ffmpeg_archive_path}")
        endif()

        file(WRITE "${_ffmpeg_extract_stamp}" "${FFMPEG_PACKAGE_VERSION}\n${FFMPEG_PACKAGE_TARGET}\n")
    endif()

    _ffmpeg_find_root(_ffmpeg_root "${_ffmpeg_extract_dir}")

    set(_ffmpeg_include_dir "${_ffmpeg_root}/include")
    set(_ffmpeg_bin_dir "${_ffmpeg_root}/bin")
    set(_ffmpeg_lib_dir "${_ffmpeg_root}/lib")

    set(_ffmpeg_required_libs avcodec avformat avutil swresample swscale)
    set(_ffmpeg_library_paths "")
    foreach(_ffmpeg_lib IN LISTS _ffmpeg_required_libs)
        set(_ffmpeg_lib_path "${_ffmpeg_lib_dir}/${_ffmpeg_lib}.lib")
        if(NOT EXISTS "${_ffmpeg_lib_path}")
            message(FATAL_ERROR "Failed to locate ${_ffmpeg_lib}.lib under: ${_ffmpeg_lib_dir}")
        endif()
        list(APPEND _ffmpeg_library_paths "${_ffmpeg_lib_path}")
    endforeach()

    file(GLOB _ffmpeg_runtime_dlls "${_ffmpeg_bin_dir}/*.dll")
    if(NOT _ffmpeg_runtime_dlls)
        message(FATAL_ERROR "Failed to locate FFmpeg runtime DLLs under: ${_ffmpeg_bin_dir}")
    endif()

    set(FFMPEG_ROOT_DIR "${_ffmpeg_root}" CACHE PATH "Resolved FFmpeg package root directory" FORCE)
    set(FFMPEG_INCLUDE_DIR "${_ffmpeg_include_dir}" CACHE PATH "Resolved FFmpeg include directory" FORCE)
    set(FFMPEG_LIBRARY_DIR "${_ffmpeg_lib_dir}" CACHE PATH "Resolved FFmpeg library directory" FORCE)
    set(FFMPEG_BIN_DIR "${_ffmpeg_bin_dir}" CACHE PATH "Resolved FFmpeg bin directory" FORCE)
    set(FFMPEG_RUNTIME_DLLS "${_ffmpeg_runtime_dlls}" CACHE STRING "Resolved FFmpeg runtime DLLs" FORCE)

    if(NOT TARGET ffmpeg_prebuilt)
        add_library(ffmpeg_prebuilt INTERFACE IMPORTED GLOBAL)
    endif()

    set_target_properties(
        ffmpeg_prebuilt
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${_ffmpeg_library_paths}"
    )
endfunction()
