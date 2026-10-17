# The only group that may include OpenGL headers: the rendering backend,
# OpenGL ES 3.2, or 3.3 core without it.  The context itself comes from windev; the Khronos headers
# from cmake/FetchGLES.cmake.
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
# Marks the gl group's own sources: its internal headers #error without it.
target_compile_definitions(gl PRIVATE OPENROO_GL_BACKEND)
target_link_libraries(gl
    PUBLIC  windev renderapi
    PRIVATE sysdev log prof gles_headers)
