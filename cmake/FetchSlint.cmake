include_guard(GLOBAL)

include(FetchContent)

set(
    SLINT_GIT_TAG
    "v1.15.1"
    CACHE STRING
    "Pinned Slint git tag"
)
set(
    SLINT_FETCHCONTENT_BASE_DIR
    "${PROJECT_SOURCE_DIR}/third_party/_deps"
    CACHE PATH
    "Base directory used by CMake FetchContent for Slint"
)

function(ensure_slint_source)
    if(NOT WIN32)
        message(FATAL_ERROR "The current build is configured only for Windows Slint builds.")
    endif()

    # Keep the fetched dependency outside the build directory so that build.cmd clean
    # does not force a full re-download on the next configure.
    set(FETCHCONTENT_BASE_DIR "${SLINT_FETCHCONTENT_BASE_DIR}" CACHE PATH "" FORCE)

    # The application uses slint::interpreter::ComponentCompiler at runtime, so the
    # interpreter feature must be enabled in the Slint runtime build.
    set(SLINT_FEATURE_INTERPRETER ON CACHE BOOL "Enable Slint interpreter support" FORCE)

    FetchContent_Declare(
        Slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG ${SLINT_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR api/cpp
    )

    FetchContent_MakeAvailable(Slint)
endfunction()
