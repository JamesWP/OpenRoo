# The only group that may use the window, event loop, message box and
# launcher: all on SDL.  It owns the launcher's artwork, compiled in from
# data/launcher and data/openroo.ico; the program has no Win32 resources.  It
# depends on nothing else in the project.
add_library(windev OBJECT
    launcher.cpp
    window.cpp
    "${CMAKE_BINARY_DIR}/generated/launcher/launcher_assets.cpp")
target_include_directories(windev PRIVATE .)

# launcher_assets.cpp and launcher_layout.h: the sliced bitmaps and icon.
set(LAUNCHER_DIR "${CMAKE_BINARY_DIR}/generated/launcher")
file(GLOB LAUNCHER_BMPS "${PROJECT_SOURCE_DIR}/data/launcher/*.bmp")
add_custom_command(
    OUTPUT "${LAUNCHER_DIR}/launcher_layout.h" "${LAUNCHER_DIR}/launcher_assets.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LAUNCHER_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/slice_launcher.py"
            "${PROJECT_SOURCE_DIR}/data/launcher" "${LAUNCHER_DIR}"
    DEPENDS ${LAUNCHER_BMPS} "${PROJECT_SOURCE_DIR}/data/openroo.ico"
            "${PROJECT_SOURCE_DIR}/tools/slice_launcher.py"
    COMMENT "Slicing the launcher bitmaps")
add_custom_target(launcher_layout DEPENDS "${LAUNCHER_DIR}/launcher_layout.h")

# BUILD_GIT_SHA for the launcher, rewritten only when it changes.
set(BUILDINFO_DIR "${CMAKE_BINARY_DIR}/generated/buildinfo")
add_custom_target(buildinfo ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DOUT=${BUILDINFO_DIR}/buildinfo.h -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS "${BUILDINFO_DIR}/buildinfo.h"
    COMMENT "Recording the git commit")

add_dependencies(windev launcher_layout buildinfo)
target_include_directories(windev PRIVATE "${LAUNCHER_DIR}" "${BUILDINFO_DIR}")
target_compile_definitions(windev PRIVATE
    OPENROO_NAME="Open'Roo" OPENROO_VERSION="${PROJECT_VERSION}")
target_link_libraries(windev PRIVATE sdl3)
