add_library(core OBJECT
    assetio.cpp
    clock.cpp
    determinism.cpp
    game.cpp
    gameglobals.cpp
    gamereset.cpp
    gamestate.cpp
    gametick.cpp
    linkedlist.cpp
    namedlist.cpp
    worldstate.cpp)
target_include_directories(core PRIVATE
    .
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
