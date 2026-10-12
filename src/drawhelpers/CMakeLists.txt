# Drawing theme objects: the world's looks and the walk they are drawn
# through.  Knows no game state; talks to the device through renderdevice.h.
add_library(drawhelpers OBJECT
    theme.cpp
    themedraw.cpp
    uvanimator.cpp)
target_include_directories(drawhelpers PUBLIC .)

target_link_libraries(drawhelpers
    PUBLIC  math gfx fx renderapi
    PRIVATE base sysdev log)
