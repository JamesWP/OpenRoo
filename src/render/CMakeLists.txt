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
    sceneobjects.cpp
    scenequad.cpp
    theme.cpp
    themedraw.cpp)
target_include_directories(render PRIVATE
    .)

target_link_libraries(render PRIVATE imgui)
