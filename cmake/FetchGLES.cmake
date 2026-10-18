# Provides the Khronos OpenGL ES 3.2 headers (GLES3/gl32.h and the
# KHR/khrplatform.h it includes) as the interface library `gles_headers`.
#
#   include(FetchGLES)
#
# The system's copy is used when it has one (libgles-dev on Debian/Ubuntu);
# otherwise -- e.g. on Windows -- the three headers are downloaded
# at configure time into ${CMAKE_BINARY_DIR}/_deps, pinned to registry
# commits and checked by SHA-256.  -DGLES_INCLUDE_DIR=<dir> picks a directory
# holding GLES3/ and KHR/ instead.
set(GLES_REGISTRY_COMMIT 6af574a14089ccfee87efe230ebcdd8742859813
    CACHE STRING "KhronosGroup/OpenGL-Registry commit to fetch GLES3 headers from")
set(EGL_REGISTRY_COMMIT db3425b8246136faccb5e2782b5694960bd6edf1
    CACHE STRING "KhronosGroup/EGL-Registry commit to fetch khrplatform.h from")

find_path(GLES_INCLUDE_DIR GLES3/gl32.h)
if(GLES_INCLUDE_DIR AND NOT EXISTS "${GLES_INCLUDE_DIR}/KHR/khrplatform.h")
    unset(GLES_INCLUDE_DIR CACHE)
endif()

if(NOT GLES_INCLUDE_DIR)
    set(dir "${CMAKE_BINARY_DIR}/_deps/gles-headers")
    set(gl  "https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/${GLES_REGISTRY_COMMIT}/api")
    set(egl "https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/${EGL_REGISTRY_COMMIT}/api")
    foreach(entry
            "GLES3/gl32.h|${gl}|f203257863a7de3fce851efa61490907171db82082381d05f86f5d9bd39e8e17"
            "GLES3/gl3platform.h|${gl}|a9e060dae5a2b11c5a889b679692b7089a10a7e03ebfbb6cf28217f6e322fb08"
            "KHR/khrplatform.h|${egl}|7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83")
        string(REPLACE "|" ";" entry "${entry}")
        list(GET entry 0 path)
        list(GET entry 1 base)
        list(GET entry 2 sha)
        if(NOT EXISTS "${dir}/${path}")
            message(STATUS "Downloading ${base}/${path}")
            file(DOWNLOAD "${base}/${path}" "${dir}/${path}" STATUS status
                 EXPECTED_HASH SHA256=${sha})
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                file(REMOVE "${dir}/${path}")
                message(FATAL_ERROR "Could not download ${base}/${path}: ${status}\n"
                                    "Pass -DGLES_INCLUDE_DIR=<dir> holding GLES3/ and KHR/.")
            endif()
        endif()
    endforeach()
    set(GLES_INCLUDE_DIR "${dir}" CACHE PATH "Directory holding GLES3/gl32.h and KHR/" FORCE)
endif()

add_library(gles_headers INTERFACE)
target_include_directories(gles_headers SYSTEM INTERFACE "${GLES_INCLUDE_DIR}")
