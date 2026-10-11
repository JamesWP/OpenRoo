# Talks to the device through renderdevice.h; gl implements it.
add_library(render OBJECT
    bridgesurf.cpp
    camera.cpp
    dsoscene.cpp
    framepose.cpp
    levelplacements.cpp
    meshbatch.cpp
    objectshadows.cpp
    quadbatch.cpp
    renderstate.cpp
    scene.cpp
    scenepasses.cpp
    sceneobjects.cpp
    scenequad.cpp
    theme.cpp
    themedraw.cpp)
target_include_directories(render PUBLIC .)

target_link_libraries(render
    PUBLIC  base math gfx fx level entities renderapi
    PRIVATE sysdev log prof camera imgui)
