# Talks to the device through renderdevice.h; gl implements it.
add_library(render OBJECT
    camera.cpp
    dsoscene.cpp
    framepose.cpp
    meshbatch.cpp
    objectshadows.cpp
    particles.cpp
    quadbatch.cpp
    rendergameframe.cpp
    renderstate.cpp
    scene.cpp
    sceneobjects.cpp
    scenequad.cpp
    themedraw.cpp)
target_include_directories(render PRIVATE
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
    ../gfx ../dbg)
target_link_libraries(render PRIVATE imgui)
