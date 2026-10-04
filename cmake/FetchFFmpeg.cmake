# Downloads and unpacks a shared LGPL FFmpeg build for MinGW (BtbN's
# FFmpeg-Builds) at configure time, and defines the imported target `ffmpeg`
# (libavformat, libavcodec, libavutil, libswscale, libswresample).
#
#   include(FetchFFmpeg)
#
# The package lands in ${CMAKE_BINARY_DIR}/_deps.  To use one you already
# have, pass -DFFMPEG_ROOT=<dir> (the unpacked top, holding include/ lib/
# bin/).  The DLLs to ship beside the executable are FFMPEG_RUNTIME_DLLS.
# The build is LGPL and linked dynamically, so the DLLs stay replaceable.
set(FFMPEG_TAG  "autobuild-2026-10-03-18-14" CACHE STRING "FFmpeg-Builds release tag")
set(FFMPEG_FILE "ffmpeg-n8.1.3-14-g330caae0c1-win64-lgpl-shared-8.1" CACHE STRING
    "FFmpeg-Builds package name, without .zip")
set(FFMPEG_ROOT "" CACHE PATH "An unpacked FFmpeg MinGW shared build")

if(NOT FFMPEG_ROOT)
    set(FFMPEG_ROOT "${CMAKE_BINARY_DIR}/_deps/${FFMPEG_FILE}")
    if(NOT EXISTS "${FFMPEG_ROOT}/lib/libavcodec.dll.a")
        set(archive "${CMAKE_BINARY_DIR}/_deps/${FFMPEG_FILE}.zip")
        set(url "https://github.com/BtbN/FFmpeg-Builds/releases/download/${FFMPEG_TAG}/${FFMPEG_FILE}.zip")
        message(STATUS "Downloading ${url}")
        file(DOWNLOAD "${url}" "${archive}" STATUS status SHOW_PROGRESS)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}")
            message(FATAL_ERROR "Could not download ${url}: ${status}\n"
                                "Unpack an FFmpeg MinGW shared build and pass -DFFMPEG_ROOT=<dir>.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    endif()
endif()

add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
target_link_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/lib")
target_link_libraries(ffmpeg INTERFACE avformat avcodec avutil swscale swresample)

file(GLOB FFMPEG_RUNTIME_DLLS "${FFMPEG_ROOT}/bin/avformat-*.dll" "${FFMPEG_ROOT}/bin/avcodec-*.dll"
     "${FFMPEG_ROOT}/bin/avutil-*.dll" "${FFMPEG_ROOT}/bin/swscale-*.dll"
     "${FFMPEG_ROOT}/bin/swresample-*.dll")
