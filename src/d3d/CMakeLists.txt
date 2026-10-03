# The only group that may include Direct3D headers.
add_library(d3d OBJECT
    createdevice.cpp
    devicetexture.cpp
    pixelconvert.cpp
    renderdevice.cpp)
target_include_directories(d3d PRIVATE
    .
    ../log
    ../app
    ../core
    ../render
    ../sysdev)
