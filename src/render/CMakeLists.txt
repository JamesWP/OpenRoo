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
    rendergameframe.cpp
    renderstate.cpp
    scene.cpp
    sceneobjects.cpp
    scenequad.cpp
    theme.cpp
    themedraw.cpp)
target_include_directories(render PRIVATE
    ../fx
    ../gfx
    ../base
    ../prof
    ../inputdev
    .
    ../log
    ../audiodev
    ../sysdev
    ../windev
    ../app
    ../audio
    ../core
    ../entities
    ../level
    ../testing
    ../ui)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(render PRIVATE
    ../fx
    ../gfx ../dbg)
target_link_libraries(render PRIVATE imgui)
