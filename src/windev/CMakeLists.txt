# The only group that may use the window, event loop, message box and dialog
# APIs: SDL for the window, Win32 for the launcher dialogs.  It also owns the resource script (icon, launcher dialogs and
# bitmaps, version info).  It depends on nothing else in the project.
add_library(windev OBJECT
    launcher.cpp
    resources.cpp
    window.cpp
    ../../data/openroo.rc)
target_include_directories(windev PRIVATE .)

# launcher_layout.h and bitmaps, also read by the resource script.
set(LAUNCHER_DIR "${CMAKE_BINARY_DIR}/generated/launcher")
file(GLOB LAUNCHER_BMPS "${PROJECT_SOURCE_DIR}/data/launcher/*.bmp")
add_custom_command(
    OUTPUT "${LAUNCHER_DIR}/launcher_layout.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LAUNCHER_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/slice_launcher.py"
            "${PROJECT_SOURCE_DIR}/data/launcher" "${LAUNCHER_DIR}"
    DEPENDS ${LAUNCHER_BMPS} "${PROJECT_SOURCE_DIR}/tools/slice_launcher.py"
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
target_link_libraries(windev PRIVATE sdl3 gdi32 version shell32)
set_source_files_properties(${PROJECT_SOURCE_DIR}/data/openroo.rc PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/openroo.ico;${LAUNCHER_DIR}/launcher_layout.h"
    COMPILE_FLAGS "--include-dir=${PROJECT_SOURCE_DIR}/data --include-dir=${LAUNCHER_DIR}")
