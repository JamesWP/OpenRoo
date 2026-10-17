# The only group that may include OpenGL headers: the rendering backend,
# OpenGL ES 3.2.  The context itself comes from windev.  The Khronos headers
# are vendored in khronos/ (MIT licensed), so no system GL headers are needed.
add_library(gl OBJECT
    createdevice.cpp
    devicetexture.cpp
    drawtrace.cpp
    glapi.cpp
    particles.cpp
    pixelconvert.cpp
    renderdevice.cpp
    shaders.cpp)
target_include_directories(gl PUBLIC .)
target_include_directories(gl PRIVATE khronos)
# Marks the gl group's own sources: its internal headers #error without it.
target_compile_definitions(gl PRIVATE OPENROO_GL_BACKEND)
target_link_libraries(gl
    PUBLIC  windev renderapi
    PRIVATE sysdev log prof)
