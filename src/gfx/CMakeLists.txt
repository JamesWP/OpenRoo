add_library(gfx OBJECT
    ani.cpp
    animatedmesh.cpp
    d3dmath_common.cpp
    d3dmath_mode.cpp
    d3dmath_std.cpp
    image.cpp
    imagebmp.cpp
    imagetga.cpp
    model.cpp
    sky.cpp
    textrenderer.cpp
    texture.cpp)
target_include_directories(gfx PRIVATE
    .)
