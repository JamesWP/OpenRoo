# The only group that may include OpenGL headers: the rendering backend,
# OpenGL 3.3 core profile.  The context itself comes from windev.
add_library(gl OBJECT
    createdevice.cpp
    devicetexture.cpp
    drawtrace.cpp
    glapi.cpp
    pixelconvert.cpp
    renderdevice.cpp
    shaders.cpp)
target_include_directories(gl PRIVATE
    ../base
    .
    ../log
    ../prof
    ../app
    ../core
    ../render
    ../gfx
    ../sysdev
    ../windev)
