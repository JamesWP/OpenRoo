add_library(game OBJECT
    game.cpp)
target_include_directories(game PRIVATE
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
    ../render)
