# Defines the imported target `ffmpeg` (libavformat, libavcodec, libavutil,
# libswscale, libswresample).
#
#   include(FetchFFmpeg)
#
# On Linux these are the system's FFmpeg development packages (pkg-config); on
# Windows vcpkg's (vcpkg.json; see README.md).
if(WIN32)
    find_package(FFMPEG REQUIRED)
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg SYSTEM INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG_PKG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
    add_library(ffmpeg INTERFACE)
    target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG_PKG)
endif()
