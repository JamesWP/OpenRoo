# Downloads and unpacks the SDL3 and SDL3_mixer MinGW development packages at
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
