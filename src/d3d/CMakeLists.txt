# The only group that may include Direct3D/DirectDraw headers.
add_library(d3d OBJECT
    createdevice.cpp
    ddrawdiag.cpp
    devicetexture.cpp
    pixelconvert.cpp
    nullddraw.cpp
    renderdevice.cpp)
target_include_directories(d3d PRIVATE
    .
    ../log
    ../app
    ../core
    ../render
    ../sysdev)

# One header version everywhere, or <ddraw.h>'s own default wins.
target_compile_definitions(d3d PRIVATE DIRECTDRAW_VERSION=0x0100)
target_link_libraries(d3d PRIVATE dxguid)
