# The movie player's platform layer.  It depends on nothing else in the project.
add_library(videodev OBJECT
    player.cpp)
target_include_directories(videodev PRIVATE .)
target_link_libraries(videodev PRIVATE sdl3)
