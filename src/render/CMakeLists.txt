# Talks to the device through renderdevice.h; gl implements it.
add_library(render OBJECT
    ani.cpp
    camera.cpp
    d3dmath_common.cpp
    d3dmath_mode.cpp
    d3dmath_std.cpp
    dsoscene.cpp
    animatedmesh.cpp
    image.cpp
    imagebmp.cpp
    imagetga.cpp
    meshbatch.cpp
    model.cpp
    objectshadows.cpp
    particles.cpp
    quadbatch.cpp
    rendergameframe.cpp
    renderstate.cpp
    scene.cpp
    sceneobjects.cpp
    scenequad.cpp
    sky.cpp
    texture.cpp
    textrenderer.cpp
    themedraw.cpp)
target_include_directories(render PRIVATE
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
target_include_directories(render PRIVATE ../dbg)
target_link_libraries(render PRIVATE imgui)
