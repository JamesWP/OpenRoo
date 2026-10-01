add_library(input OBJECT
    camerainput.cpp
    inputsetup.cpp
    keypress.cpp
    progctrl.cpp)
target_include_directories(input PRIVATE
    .
    ../audiodev
    ../windev
    ../inputdev
    ../app
    ../audio
    ../core
    ../entities
    ../level
    ../render
    ../testing
    ../ui)
target_link_libraries(input PRIVATE winmm)
