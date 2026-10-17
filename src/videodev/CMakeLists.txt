# The movie player's platform layer.  It depends on nothing else in the project.
# Without OPENROO_VIDEO (no FFmpeg, e.g. the rg35xx port) it is a stub that
# plays nothing.
if(OPENROO_VIDEO)
    add_library(videodev OBJECT player.cpp)
    target_link_libraries(videodev PRIVATE sdl3 ffmpeg)
else()
    add_library(videodev OBJECT player_none.cpp)
endif()
target_include_directories(videodev PUBLIC .)
