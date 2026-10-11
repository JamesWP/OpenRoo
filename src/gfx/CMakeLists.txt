add_library(gfx OBJECT
    ani.cpp
    animatedmesh.cpp
    d3dmath.cpp
    image.cpp
    imagebmp.cpp
    imagetga.cpp
    model.cpp
    sky.cpp
    textrenderer.cpp
    texture.cpp)
target_include_directories(gfx PUBLIC .)
target_link_libraries(gfx
    PUBLIC  math renderapi
    PRIVATE base sysdev log)
