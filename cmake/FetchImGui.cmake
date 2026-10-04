# Downloads and unpacks Dear ImGui at configure time, and defines the static
# library `imgui` (the core, without any backend).
#
#   include(FetchImGui)
#
# The source lands in ${CMAKE_BINARY_DIR}/_deps, so a fresh build directory
# needs the network once.  To use a copy you already have, point
# -DIMGUI_ROOT=<dir> at its top (the one holding imgui.h).  IMGUI_DIR is the
# directory in use; the SDL3 platform backend is compiled by windev from
# ${IMGUI_DIR}/backends, and the renderer is our own (src/debugui).
set(IMGUI_VERSION 1.92.9b CACHE STRING "Dear ImGui release to fetch")
set(IMGUI_SHA256  21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    CACHE STRING "SHA-256 of the release tarball (empty to skip the check)")
set(IMGUI_ROOT "" CACHE PATH "An unpacked Dear ImGui source tree")

if(IMGUI_ROOT)
    set(IMGUI_DIR "${IMGUI_ROOT}")
else()
    set(IMGUI_DIR "${CMAKE_BINARY_DIR}/_deps/imgui-${IMGUI_VERSION}")
    if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
        set(archive "${CMAKE_BINARY_DIR}/_deps/imgui-${IMGUI_VERSION}.tar.gz")
        set(url "https://github.com/ocornut/imgui/archive/refs/tags/v${IMGUI_VERSION}.tar.gz")
        message(STATUS "Downloading ${url}")
        if(IMGUI_SHA256)
            set(hash EXPECTED_HASH SHA256=${IMGUI_SHA256})
        endif()
        file(DOWNLOAD "${url}" "${archive}" STATUS status SHOW_PROGRESS ${hash})
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}")
            message(FATAL_ERROR "Could not download ${url}: ${status}\n"
                                "Unpack it yourself and pass -DIMGUI_ROOT=<dir>.")
        endif()
        file(MAKE_DIRECTORY "${IMGUI_DIR}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${IMGUI_DIR}")
        # The archive has one top-level directory; hoist its contents.
        file(GLOB inner "${IMGUI_DIR}/imgui-*")
        if(inner)
            file(GLOB parts "${inner}/*")
            foreach(p IN LISTS parts)
                file(COPY "${p}" DESTINATION "${IMGUI_DIR}")
            endforeach()
            file(REMOVE_RECURSE "${inner}")
        endif()
    endif()
endif()

# Third-party code: built without our warnings.  The Win32 helpers are off
# (clipboard and IME come from SDL).
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_WIN32_FUNCTIONS)
target_compile_options(imgui PRIVATE -w)
