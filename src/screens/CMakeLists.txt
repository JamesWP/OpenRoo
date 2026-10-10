add_library(screens OBJECT
    levelselect.cpp
    menunav.cpp
    menuscreens.cpp
    scoreoverlay.cpp)
target_include_directories(screens PRIVATE
    ../game
    .
    ../base
    ../log
    ../sysdev
    ../prof
    ../audiodev
    ../windev
    ../inputdev
    ../gfx
    ../fx
    ../audio
    ../level
    ../entities
    ../testing
    ../input
    ../ui
    ../core
    ../render
    ../app)
