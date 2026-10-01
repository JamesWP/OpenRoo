add_library(app OBJECT
    config.cpp
    gamelog.cpp
    launcher.cpp
    launcherdialogs.cpp
    log.cpp
    main.cpp
    process.cpp
    resources.cpp
    veh.cpp)
target_include_directories(app PRIVATE
    .
    ../audio
    ../core
    ../d3d
    ../entities
    ../input
    ../level
    ../render
    ../ui)

# The launcher's bitmaps and layout header (launcher_layout.h), cut from three
# mock-ups.  Also read by the resource script.
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

# The commit being built, for the launcher's corner text.  Rewritten only when
# it changes, so an unchanged tree does not rebuild.
set(BUILDINFO_DIR "${CMAKE_BINARY_DIR}/generated/buildinfo")
add_custom_target(buildinfo ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DOUT=${BUILDINFO_DIR}/buildinfo.h -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS "${BUILDINFO_DIR}/buildinfo.h"
    COMMENT "Recording the git commit")

add_dependencies(app launcher_layout buildinfo)
target_include_directories(app PRIVATE "${LAUNCHER_DIR}" "${BUILDINFO_DIR}")
