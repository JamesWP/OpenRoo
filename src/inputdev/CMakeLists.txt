# The only group that may poll keys.  It depends on nothing else in the project
# but the key interface it implements.
add_library(inputdev OBJECT
    sdlinput.cpp)
target_include_directories(inputdev PUBLIC .)
target_link_libraries(inputdev PUBLIC inputapi PRIVATE sdl3)
