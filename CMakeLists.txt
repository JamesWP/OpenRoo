cmake_minimum_required(VERSION 3.20)

# Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
project(OpenRoo CXX RC)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_compile_options(-Wall -Wextra)

# One directory under src/ is one group.  A group's include path is its own
# folder plus its dependencies' (private), so the lists in src/*/CMakeLists.txt
# are the layering and the compiler enforces them.  Today's lists are the
# cyclic baseline, to be narrowed.
foreach(group app audio core d3d entities input level render testing ui)
    add_subdirectory(src/${group})
endforeach()

set(objects)
foreach(group app audio core d3d entities input level render testing ui)
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
set_source_files_properties(data/openroo.rc PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/openroo.ico;${CMAKE_BINARY_DIR}/generated/launcher/launcher_layout.h")
set_source_files_properties(data/openroo.rc PROPERTIES
    COMPILE_FLAGS "--include-dir=${PROJECT_SOURCE_DIR}/data --include-dir=${CMAKE_BINARY_DIR}/generated/launcher")
add_dependencies(KarooOwn launcher_layout buildinfo)

# Only the d3d group may include the system Direct3D headers.
add_custom_target(check-native-d3d ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeD3D.cmake)
add_dependencies(KarooOwn check-native-d3d)
