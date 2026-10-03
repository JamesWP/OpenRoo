add_library(core OBJECT
    clock.cpp
    determinism.cpp
    game.cpp
    gameglobals.cpp
    gamereset.cpp
    gamestate.cpp
    gametick.cpp
    worldstate.cpp)
target_include_directories(core PRIVATE
    .
    ../log
    ../audiodev
    ../sysdev
    ../windev
    ../inputdev
    ../app
    ../audio
    ../entities
    ../input
    ../level
    ../render
    ../testing
    ../ui)
