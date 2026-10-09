add_library(core OBJECT
    clock.cpp
    determinism.cpp
    game.cpp
    gameglobals.cpp
    gamereset.cpp
    gamestate.cpp
    ini.cpp
    gamedebug.cpp
    gametick.cpp)
target_include_directories(core PRIVATE
    ../prof
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

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(core PRIVATE ../dbg)
target_link_libraries(core PRIVATE imgui)
