cmake_minimum_required(VERSION 3.20)

# Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/mingw.cmake
project(OpenRoo CXX RC)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_compile_options(-Wall -Wextra)

# SDL3 (tools/fetch_sdl3.sh), the platform groups' only dependency besides
# Direct3D.  Imported, so only the groups that name it see its headers.
set(SDL3_ROOT "${PROJECT_SOURCE_DIR}/third_party/SDL3" CACHE PATH "SDL3 MinGW development package")
set(SDL3_PREFIX "${SDL3_ROOT}/x86_64-w64-mingw32")
if(NOT EXISTS "${SDL3_PREFIX}/lib/libSDL3.dll.a")
    message(FATAL_ERROR "SDL3 not found in ${SDL3_ROOT}: run tools/fetch_sdl3.sh or pass -DSDL3_ROOT")
endif()
add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_IMPLIB "${SDL3_PREFIX}/lib/libSDL3.dll.a"
    IMPORTED_LOCATION "${SDL3_PREFIX}/bin/SDL3.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL3_PREFIX}/include")

# One directory under src/ is one group.  A group's include path is its own
# folder plus its dependencies' (private), so the lists in src/*/CMakeLists.txt
# are the layering and the compiler enforces them.  Today's lists are the
# cyclic baseline, to be narrowed.
foreach(group app audio audiodev core d3d entities input inputdev level render testing ui videodev windev sysdev log)
    add_subdirectory(src/${group})
endforeach()

add_executable(KarooOwn WIN32
    src/windev/exemain.cpp)
set_target_properties(KarooOwn PROPERTIES OUTPUT_NAME KarooOwn)
target_include_directories(KarooOwn PRIVATE src/app)
target_link_libraries(KarooOwn PRIVATE
    app audio audiodev core d3d entities input inputdev level render testing ui videodev windev sysdev log
    -static-libgcc -static-libstdc++ -static)

# SDL3.dll next to the executable (launch.sh copies both into run/).
add_custom_command(TARGET KarooOwn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_PREFIX}/bin/SDL3.dll" $<TARGET_FILE_DIR:KarooOwn>)

# Only the platform groups may include SDL.
add_custom_target(check-native-sdl ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeSDL.cmake)
add_dependencies(KarooOwn check-native-sdl)

# Only the audiodev group may include the system audio headers.
add_custom_target(check-native-audio ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeAudio.cmake)
add_dependencies(KarooOwn check-native-audio)

# Only the platform groups may use the system clock, time and exception APIs.
add_custom_target(check-native-system ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeSystem.cmake)
add_dependencies(KarooOwn check-native-system)

# Only the windev group may use the system window and dialog APIs.
add_custom_target(check-native-window ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeWindow.cmake)
add_dependencies(KarooOwn check-native-window)

# Only the inputdev group may include the system input headers.
add_custom_target(check-native-input ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeInput.cmake)
add_dependencies(KarooOwn check-native-input)

# Only the platform groups may include <windows.h> from a header.
add_custom_target(check-native-windows ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeWindows.cmake)
add_dependencies(KarooOwn check-native-windows)

# Only the d3d group may include the system Direct3D headers.
add_custom_target(check-native-d3d ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeD3D.cmake)
add_dependencies(KarooOwn check-native-d3d)
