cmake_minimum_required(VERSION 3.20)

# The platform to build for: -DOPENROO_PLATFORM=linux (the default on Linux;
# native), -DOPENROO_PLATFORM=windows (the default on Windows; native, with MSVC
# and the dependencies from vcpkg -- see the `windows` preset in
# CMakePresets.json) or -DOPENROO_PLATFORM=rg35xx (the Anbernic RG35XX Plus
# handheld, cross-compiled with the SDK from github.com/JamesWP/rg35xx-plus-sdk;
# point -DRG35XX_SDK=<dir> at the unpacked SDK, or at that repo's output/host).
if(CMAKE_HOST_WIN32)
    set(platform_default windows)
else()
    set(platform_default linux)
endif()
set(OPENROO_PLATFORM "${platform_default}" CACHE STRING "linux, windows or rg35xx")
set_property(CACHE OPENROO_PLATFORM PROPERTY STRINGS linux windows rg35xx)
if(OPENROO_PLATFORM STREQUAL "windows")
    if(NOT CMAKE_HOST_WIN32)
        message(FATAL_ERROR "OPENROO_PLATFORM=windows builds on Windows (see README.md); it is not cross-compiled")
    endif()
elseif(OPENROO_PLATFORM STREQUAL "linux")
    if(CMAKE_HOST_WIN32)
        message(FATAL_ERROR "OPENROO_PLATFORM=linux builds on Linux (or WSL)")
    endif()
elseif(OPENROO_PLATFORM STREQUAL "rg35xx")
    set(RG35XX_SDK "$ENV{RG35XX_SDK}" CACHE PATH "The RG35XX Plus SDK (holding share/buildroot/toolchainfile.cmake)")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        if(NOT EXISTS "${RG35XX_SDK}/share/buildroot/toolchainfile.cmake")
            message(FATAL_ERROR "OPENROO_PLATFORM=rg35xx needs -DRG35XX_SDK=<dir> (or $RG35XX_SDK): "
                                "the SDK holding share/buildroot/toolchainfile.cmake")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "${RG35XX_SDK}/share/buildroot/toolchainfile.cmake")
    endif()
else()
    message(FATAL_ERROR "OPENROO_PLATFORM must be linux, windows or rg35xx, not '${OPENROO_PLATFORM}'")
endif()

# The intro movie needs FFmpeg, which the rg35xx SDK does not have.
if(OPENROO_PLATFORM STREQUAL "rg35xx")
    set(video_default OFF)
else()
    set(video_default ON)
endif()
option(OPENROO_VIDEO "Play the intro movie (needs FFmpeg)" ${video_default})

project(OpenRoo VERSION 0.1.0 LANGUAGES CXX)

# Optimised unless told otherwise (-DCMAKE_BUILD_TYPE=Debug for a debugger
# session).  An unoptimised build loads a level about 4x slower, which is most
# of what the replay suite spends.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD 17)  # inline variables: GCC defaults to it, MSVC does not
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# SDL3 and SDL3_mixer, fetched here (cmake/FetchSDL.cmake): the platform
# groups' only dependency.  Imported, so only the groups that
# name them see their headers.
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(FetchSDL)
if(OPENROO_VIDEO)
    include(FetchFFmpeg)  # the intro movie's decoder
endif()
include(FetchImGui)   # the debug UI
include(FetchGLES)    # the OpenGL ES headers for src/gl

# Warnings are errors for our own groups only; set after the fetched
# dependencies are added so it does not reach them.
if(MSVC)
    # /utf-8: sources are UTF-8.  vcpkg's toolchain copies the SDL and FFmpeg
    # DLLs next to the executable.
    add_compile_options(/W3 /WX /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    include_directories(BEFORE cmake/msvc)  # the POSIX <strings.h>
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# One directory under src/ is one group.  Each group declares the groups it
# depends on with target_link_libraries in its own CMakeLists.txt; their include
# paths follow from those links.  tools/check_layers.py (the check-layers
# target) checks the declarations against the includes and src/layers.yaml.
set(GROUPS app audio base camera script session fx game gfx math screens audiodev core dbg debugui entities sequencer drawhelpers gl input inputapi inputdev level render renderapi sim bot botapi botmodel botmotor botplan botworld bottactics botexec soundapi testing ui videodev windev sysdev log prof)
foreach(group ${GROUPS})
    add_subdirectory(src/${group})
endforeach()

if(WIN32)
    add_executable(OpenRoo WIN32 src/app/exemain_windows.cpp)
else()
    add_executable(OpenRoo src/app/exemain_linux.cpp)
endif()
set_target_properties(OpenRoo PROPERTIES OUTPUT_NAME OpenRoo)
target_include_directories(OpenRoo PRIVATE src/app)
target_link_libraries(OpenRoo PRIVATE
    app audio audiodev base camera script session fx sequencer drawhelpers game gfx math screens core dbg debugui entities gl input inputapi inputdev level render sim bot botapi botmodel botmotor botplan botworld bottactics botexec soundapi testing ui videodev windev sysdev log prof imgui)
if(OPENROO_PLATFORM STREQUAL "rg35xx")
    # The SDK's SDL3 (with the device's video driver) ships in lib/ beside the
    # executable.  DT_RPATH, not DT_RUNPATH, so it wins over whatever
    # LD_LIBRARY_PATH the device's launcher sets.
    set_target_properties(OpenRoo PROPERTIES BUILD_RPATH "$ORIGIN/lib")
    target_link_options(OpenRoo PRIVATE -Wl,--disable-new-dtags)
    # build/rg35xx/OpenRoo: the app directory to copy to the SD card, less the
    # game data (see data/rg35xx/mux_launch.sh).
    set(pkg "${CMAKE_BINARY_DIR}/rg35xx/OpenRoo")
    add_custom_target(rg35xx-package
        COMMAND ${CMAKE_COMMAND} -E make_directory "${pkg}/lib"
        COMMAND ${CMAKE_STRIP} -o "${pkg}/OpenRoo" $<TARGET_FILE:OpenRoo>
        COMMAND ${CMAKE_STRIP} -o "${pkg}/lib/libSDL3.so.0" "$<TARGET_SONAME_FILE:SDL3::SDL3-shared>"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${PROJECT_SOURCE_DIR}/data/rg35xx/mux_launch.sh"
                "${PROJECT_SOURCE_DIR}/data/rg35xx/openroo.ini.default" "${pkg}"
        DEPENDS OpenRoo
        COMMENT "Packaging ${pkg}")
endif()

# Only the platform groups may include SDL.
add_custom_target(check-native-sdl ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeSDL.cmake)
add_dependencies(OpenRoo check-native-sdl)

# Only the audiodev group may include the system audio headers.
add_custom_target(check-native-audio ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeAudio.cmake)
add_dependencies(OpenRoo check-native-audio)

# The groups' declared links and includes must match, and respect the layering
# in src/layers.yaml.
add_custom_target(check-layers ALL
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_layers.py ${PROJECT_SOURCE_DIR}
    COMMENT "Checking the group layering")
add_dependencies(OpenRoo check-layers)
# Every group waits for the check, so a layering error fails the build before
# anything compiles.
foreach(group ${GROUPS})
    if(TARGET ${group})
        add_dependencies(${group} check-layers)
    endif()
endforeach()

# A picture of the groups and their links: build/groups.svg (on request only).
add_custom_target(group-graph
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/group_graph.py
            -o ${CMAKE_BINARY_DIR}/groups.svg ${PROJECT_SOURCE_DIR}
    COMMENT "Drawing the group graph")

# Only the platform groups may use the system clock, time and exception APIs.
add_custom_target(check-native-system ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeSystem.cmake)
add_dependencies(OpenRoo check-native-system)

# Only the windev group may use the system window and dialog APIs.
add_custom_target(check-native-window ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeWindow.cmake)
add_dependencies(OpenRoo check-native-window)

# Only the inputdev group may include the system input headers.
add_custom_target(check-native-input ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeInput.cmake)
add_dependencies(OpenRoo check-native-input)

# Only the platform groups may include <windows.h> from a header.
add_custom_target(check-native-windows ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeWindows.cmake)
add_dependencies(OpenRoo check-native-windows)

# Only the gl group may include the system OpenGL headers.
add_custom_target(check-native-gl ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeGL.cmake)
add_dependencies(OpenRoo check-native-gl)
