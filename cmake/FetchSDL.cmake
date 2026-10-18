# Provides the interface libraries `sdl3` and `sdl3_mixer`.
#
#   include(FetchSDL)
#
# On Windows both come from vcpkg (vcpkg.json; the toolchain file is set by the
# `windows` preset, see README.md).  On Linux they are fetched as source
# (FetchContent) and built with the project, unless the system already has them.
set(SDL3_VERSION       3.4.18 CACHE STRING "SDL3 release to fetch (Linux, when not installed)")
set(SDL3_MIXER_VERSION 3.2.4  CACHE STRING "SDL3_mixer release to fetch (Linux, when not installed)")

if(WIN32)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_mixer CONFIG REQUIRED)
    add_library(sdl3 INTERFACE)
    target_link_libraries(sdl3 INTERFACE SDL3::SDL3)
    add_library(sdl3_mixer INTERFACE)
    target_link_libraries(sdl3_mixer INTERFACE SDL3_mixer::SDL3_mixer sdl3)
    return()
endif()

# Prefer SDL3 / SDL3_mixer installed on the system (e.g. Ubuntu 26's
# libsdl3-dev); each is fetched as source only if it is not found.
# -DOPENROO_FETCH_SDL=ON skips the system copies.
option(OPENROO_FETCH_SDL "Always build SDL3 and SDL3_mixer from source on Linux" OFF)
set(sdl_fetch SDL3 SDL3_mixer)
if(NOT OPENROO_FETCH_SDL)
    find_package(SDL3 3.2 QUIET CONFIG)
    if(SDL3_FOUND)
        list(REMOVE_ITEM sdl_fetch SDL3)
        find_package(SDL3_mixer 3.0 QUIET CONFIG)
        if(SDL3_mixer_FOUND)
            list(REMOVE_ITEM sdl_fetch SDL3_mixer)
        endif()
    endif()
endif()
message(STATUS "SDL3: ${SDL3_FOUND}, SDL3_mixer: ${SDL3_mixer_FOUND} (system); fetching: ${sdl_fetch}")
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND_LIBDECOR OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLMIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-${SDL3_VERSION} GIT_SHALLOW TRUE)
FetchContent_Declare(SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-${SDL3_MIXER_VERSION} GIT_SHALLOW TRUE)
if(sdl_fetch)
    FetchContent_MakeAvailable(${sdl_fetch})
endif()
add_library(sdl3 INTERFACE)
target_link_libraries(sdl3 INTERFACE SDL3::SDL3)
add_library(sdl3_mixer INTERFACE)
target_link_libraries(sdl3_mixer INTERFACE SDL3_mixer::SDL3_mixer sdl3)
