add_library(testing OBJECT
    record.cpp)
target_include_directories(testing PRIVATE
    ../base
    ../fx
    .
    ../log
    ../audiodev
    ../inputdev
    ../app
    ../audio
    ../core
    ../entities
    ../input
    ../level
    ../render
    ../gfx
    ../ui
    ../sysdev)
