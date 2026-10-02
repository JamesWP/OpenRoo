# Talks to the device through renderdevice.h; d3d implements it.
add_library(render OBJECT
    ani.cpp
    camera.cpp
    d3dmath_common.cpp
    d3dmath_mode.cpp
    d3dmath_std.cpp
    dsoscene.cpp
    faktmesh.cpp
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
    textrenderer.cpp
    themedraw.cpp)
target_include_directories(render PRIVATE
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
