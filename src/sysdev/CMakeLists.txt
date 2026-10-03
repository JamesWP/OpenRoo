# The platform system layer: clocks, local time, the executable directory, the crash
# logger.  It depends on nothing else in the project.
add_library(sysdev OBJECT
    system.cpp)
target_include_directories(sysdev PRIVATE .)
target_link_libraries(sysdev PRIVATE sdl3)
