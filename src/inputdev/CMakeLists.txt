# The only group that may poll keys.  It depends on nothing else in the project.
add_library(inputdev OBJECT
    sdlinput.cpp)
target_include_directories(inputdev PRIVATE .)
target_link_libraries(inputdev PRIVATE sdl3)
