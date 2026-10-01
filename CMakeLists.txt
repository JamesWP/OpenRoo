cmake_minimum_required(VERSION 3.20)

# Cross-compiled; pass the toolchain on the first configure:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
# (the Makefile wrapper does this).
project(OpenRoo CXX RC)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# One DirectDraw header version for every file.  It only selects what the
# name DDCAPS means, and nothing here takes sizeof(DDCAPS); defining it per
# header let <ddraw.h>'s own default (0x0700) win whenever it came first.
add_compile_options(-Wall -Wextra)
add_compile_definitions(DIRECTDRAW_VERSION=0x0100)

# ─── Component groups ─────────────────────────────────────────────────────
#
# One directory under src/ is one group: an OBJECT library, so every object is
# linked whether or not anything references it.  A group's include path is its
# own folder plus the folders of the groups it depends on, and nothing else.
# The code names headers by bare name, so a header in any other group is "file
# not found": those include lists ARE the layering, and the compiler checks
# them.  Dependencies are private: if A's header includes B's, whoever includes
# A's header lists B as well.
#
#   d3d      the only group that may include Direct3D/DirectDraw headers
#            (enforced); render defines the RenderDevice interface it implements
#
# The other groups' lists are the baseline the code has today, taken from what
# each actually includes.  They are cyclic -- everything reaches into everything
# -- and are to be narrowed one rule at a time (a math group depending on
# nothing, audio not depending on render, ...).  Deleting an entry makes the
# compiler list every include that relied on it.
foreach(group app audio core d3d entities input level render testing ui)
    add_subdirectory(src/${group})
endforeach()

# ─── The game ─────────────────────────────────────────────────────────────
set(groups app audio core d3d entities input level render testing ui)
set(objects)
foreach(group IN LISTS groups)
    list(APPEND objects $<TARGET_OBJECTS:${group}>)
endforeach()

add_executable(KarooOwn WIN32
    ${objects}
    src/app/exemain.cpp
    data/openroo.rc)
set_target_properties(KarooOwn PROPERTIES OUTPUT_NAME KarooOwn)
target_include_directories(KarooOwn PRIVATE src/app)
target_link_libraries(KarooOwn PRIVATE
    gdi32 version shell32 winmm dsound dinput8 dxguid
    -static-libgcc -static-libstdc++ -static)
# Everything the resource script includes: the icon and the launcher layout.
set_source_files_properties(data/openroo.rc PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/openroo.ico;${CMAKE_BINARY_DIR}/generated/launcher/launcher_layout.h")
set_source_files_properties(data/openroo.rc PROPERTIES
    COMPILE_FLAGS "--include-dir=${PROJECT_SOURCE_DIR}/data --include-dir=${CMAKE_BINARY_DIR}/generated/launcher")
add_dependencies(KarooOwn launcher_layout buildinfo)

# Only the d3d group may include the system Direct3D headers.
add_custom_target(check-native-d3d ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeD3D.cmake)
add_dependencies(KarooOwn check-native-d3d)
