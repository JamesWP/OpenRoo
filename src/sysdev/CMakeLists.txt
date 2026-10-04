# The platform system layer: clocks, local time, the executable directory, file
# names.  One header (sysdev.h); the .cpp files named _windows and _linux are
# chosen by platform, those named _common serve both.  It depends on nothing
# else in the project.
if(WIN32)
    set(SYSDEV_PLATFORM path_windows.cpp textfile_windows.cpp)
else()
    set(SYSDEV_PLATFORM path_linux.cpp textfile_linux.cpp)
endif()
add_library(sysdev OBJECT
    system.cpp
    text_common.cpp
    textfile_common.cpp
    ${SYSDEV_PLATFORM})
target_include_directories(sysdev PRIVATE .)
target_link_libraries(sysdev PRIVATE sdl3)
