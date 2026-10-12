# The level's fixed picture, the camera, the scene objects and the render
# setup.  Talks to the device through renderdevice.h; gl implements it.
add_library(render OBJECT
    camera.cpp
    dsoscene.cpp
    framepose.cpp
    levelplacements.cpp
    renderstate.cpp
    scene.cpp)
target_include_directories(render PUBLIC .)

target_link_libraries(render
    PUBLIC  math gfx renderapi drawhelpers
    PRIVATE base sysdev log prof fx camera level entities imgui)
