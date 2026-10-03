# The only group that may include the system audio headers (SDL).  It depends
# on nothing else in the project.
add_library(audiodev OBJECT
    buffer.cpp
    device.cpp
    music.cpp
    stream.cpp
    system.cpp
    voice.cpp
    wav.cpp)
target_include_directories(audiodev PRIVATE .)
target_link_libraries(audiodev PRIVATE sdl3)
