add_library(input OBJECT
    camerainput.cpp
    inputsetup.cpp
    keypress.cpp
    progctrl.cpp)
target_include_directories(input PRIVATE
    ../fx
    ../base
    .
    ../log
    ../audiodev
    ../windev
    ../inputdev
    ../app
    ../audio
    ../core
    ../entities
    ../level
    ../render
    ../gfx
    ../testing
    ../ui
    ../sysdev)
