cmake_minimum_required(VERSION 3.20)

# The platform to build for: -DOPENROO_PLATFORM=linux (the default; native) or
# -DOPENROO_PLATFORM=windows (cross-compiled with MinGW-w64, run under Wine/Proton).
set(OPENROO_PLATFORM "linux" CACHE STRING "linux or windows")
set_property(CACHE OPENROO_PLATFORM PROPERTY STRINGS linux windows)
if(OPENROO_PLATFORM STREQUAL "windows")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw.cmake")
    endif()
elseif(NOT OPENROO_PLATFORM STREQUAL "linux")
    message(FATAL_ERROR "OPENROO_PLATFORM must be windows or linux, not '${OPENROO_PLATFORM}'")
endif()

project(OpenRoo VERSION 0.1.0 LANGUAGES CXX)

# Optimised unless told otherwise (-DCMAKE_BUILD_TYPE=Debug for a debugger
# session).  An unoptimised build loads a level about 4x slower, which is most
# of what the replay suite spends.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# SDL3 and SDL3_mixer, fetched here (cmake/FetchSDL.cmake): the platform
# groups' only dependency.  Imported, so only the groups that
# name them see their headers.
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(FetchSDL)
include(FetchFFmpeg)  # the intro movie's decoder
include(FetchImGui)   # the debug UI

# Warnings are errors for our own groups only; set after the fetched
# dependencies are added so it does not reach them.
add_compile_options(-Wall -Wextra -Werror)

# One directory under src/ is one group.  A group's include path is its own
# folder plus its dependencies' (private), so the lists in src/*/CMakeLists.txt
# are the layering and the compiler enforces them.  Today's lists are the
# cyclic baseline, to be narrowed.
foreach(group app audio audiodev core debugui entities gl input inputdev level render testing ui videodev windev sysdev log prof)
    add_subdirectory(src/${group})
endforeach()

if(WIN32)
    add_executable(OpenRoo WIN32 src/windev/exemain_windows.cpp)
else()
    add_executable(OpenRoo src/windev/exemain_linux.cpp)
endif()
set_target_properties(OpenRoo PROPERTIES OUTPUT_NAME OpenRoo)
target_include_directories(OpenRoo PRIVATE src/app)
target_link_libraries(OpenRoo PRIVATE
    app audio audiodev core debugui entities gl input inputdev level render testing ui videodev windev sysdev log prof imgui)
if(WIN32)
    target_link_libraries(OpenRoo PRIVATE -static-libgcc -static-libstdc++ -static)
    # The SDL and FFmpeg DLLs next to the executable (launch.sh copies them into run/).
    add_custom_command(TARGET OpenRoo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL_RUNTIME_DLLS} ${FFMPEG_RUNTIME_DLLS} $<TARGET_FILE_DIR:OpenRoo>)
endif()

# Only the platform groups may include SDL.
add_custom_target(check-native-sdl ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeSDL.cmake)
add_dependencies(OpenRoo check-native-sdl)

# Only the audiodev group may include the system audio headers.
add_custom_target(check-native-audio ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeAudio.cmake)
add_dependencies(OpenRoo check-native-audio)

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
