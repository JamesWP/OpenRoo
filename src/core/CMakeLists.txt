add_library(core OBJECT
    game.cpp
    checkpoint.cpp
    gamereset.cpp
    levelsetup.cpp
    levelsounds.cpp
    levelentry.cpp
    levelscore.cpp
    levelparse.cpp
    tiledebug.cpp
    gamestate.cpp
    gamedebug.cpp
    gametick.cpp)
target_include_directories(core PRIVATE
    ../fx
    ../base
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
    ../gfx
    ../testing
    ../ui)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(core PRIVATE
    ../fx ../dbg)
target_link_libraries(core PRIVATE imgui)
