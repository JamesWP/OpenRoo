# The Direct3D/DirectDraw backend.  The system Direct3D headers are only
# allowed here (cmake/CheckNativeD3D.cmake); the project's own d3dnative.h and
# ddrawdiag.h are hidden from groups that do not list d3d.
# Only app lists it under DEPENDS (for nullddraw.h).
add_library(d3d OBJECT
    createdevice.cpp
    ddrawdiag.cpp
    nullddraw.cpp
    renderdevice.cpp
    scenetexture.cpp
    texture.cpp
    texturedib.cpp
    texturetga.cpp)
target_include_directories(d3d PRIVATE
    .
    ../app
    ../core
    ../render)
