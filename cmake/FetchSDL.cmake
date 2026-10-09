# On Linux the two libraries are fetched as source (FetchContent) and built
# with the project, unless the system already has them.  On Windows: downloads and unpacks the SDL3 and SDL3_mixer MinGW development packages at
# configure time, and defines the imported targets `sdl3` and `sdl3_mixer`.
#
#   include(FetchSDL)
#
# Packages land in ${CMAKE_BINARY_DIR}/_deps, so a fresh build directory needs
# the network once.  To use a package you already have, point
# -DSDL3_ROOT=<dir> (likewise SDL3_MIXER_ROOT) at its unpacked top (the one
# holding x86_64-w64-mingw32/).  Each package's DLL is exposed as
# SDL_RUNTIME_DLLS, for copying beside the executable.
set(SDL3_VERSION       3.4.18 CACHE STRING "SDL3 release to fetch")
set(SDL3_MIXER_VERSION 3.2.4  CACHE STRING "SDL3_mixer release to fetch")
set(SDL3_ROOT       "" CACHE PATH "An unpacked SDL3 MinGW development package")
set(SDL3_MIXER_ROOT "" CACHE PATH "An unpacked SDL3_mixer MinGW development package")

if(NOT WIN32)
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
    set(SDL_RUNTIME_DLLS "")
    return()
endif()

function(openroo_fetch_sdl_package name repo version root_var)
    if(${root_var})
        set(root "${${root_var}}")
    else()
        set(root "${CMAKE_BINARY_DIR}/_deps/${name}-${version}")
        if(NOT EXISTS "${root}/x86_64-w64-mingw32")
            set(archive "${CMAKE_BINARY_DIR}/_deps/${name}-${version}-mingw.tar.gz")
            set(url "https://github.com/libsdl-org/${repo}/releases/download/release-${version}/${name}-devel-${version}-mingw.tar.gz")
            message(STATUS "Downloading ${url}")
            file(DOWNLOAD "${url}" "${archive}" STATUS status SHOW_PROGRESS)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                file(REMOVE "${archive}")
                message(FATAL_ERROR "Could not download ${url}: ${status}\n"
                                    "Unpack it yourself and pass -D${root_var}=<dir>.")
            endif()
            file(MAKE_DIRECTORY "${root}")
            file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${root}")
            # The archive has one top-level directory; hoist its contents.
            file(GLOB inner "${root}/${name}-*")
            if(inner)
                file(GLOB parts "${inner}/*")
                foreach(p IN LISTS parts)
                    file(COPY "${p}" DESTINATION "${root}")
                endforeach()
                file(REMOVE_RECURSE "${inner}")
            endif()
        endif()
    endif()
    set(${name}_PREFIX "${root}/x86_64-w64-mingw32" PARENT_SCOPE)
endfunction()

openroo_fetch_sdl_package(SDL3 SDL ${SDL3_VERSION} SDL3_ROOT)
openroo_fetch_sdl_package(SDL3_mixer SDL_mixer ${SDL3_MIXER_VERSION} SDL3_MIXER_ROOT)

add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_IMPLIB "${SDL3_PREFIX}/lib/libSDL3.dll.a"
    IMPORTED_LOCATION "${SDL3_PREFIX}/bin/SDL3.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL3_PREFIX}/include")

add_library(sdl3_mixer SHARED IMPORTED)
set_target_properties(sdl3_mixer PROPERTIES
    IMPORTED_IMPLIB "${SDL3_mixer_PREFIX}/lib/libSDL3_mixer.dll.a"
    IMPORTED_LOCATION "${SDL3_mixer_PREFIX}/bin/SDL3_mixer.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL3_mixer_PREFIX}/include"
    INTERFACE_LINK_LIBRARIES sdl3)

set(SDL_RUNTIME_DLLS "${SDL3_PREFIX}/bin/SDL3.dll" "${SDL3_mixer_PREFIX}/bin/SDL3_mixer.dll")
