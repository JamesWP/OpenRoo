# The frame profiler: scoped timers the update and draw code nest, and the
# debug UI reads.
add_library(prof OBJECT
    prof.cpp
    profpanel.cpp)
target_include_directories(prof PUBLIC .)
target_link_libraries(prof
    PRIVATE log imgui)
