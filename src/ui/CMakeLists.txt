add_library(ui OBJECT
    cheatcode.cpp
    highscores.cpp
    levelselect.cpp
    menunav.cpp
    menuscreens.cpp
    menutree.cpp
    saveslots.cpp
    scoreoverlay.cpp
    scoretally.cpp
    scriptplayer.cpp
    textentry.cpp
    theme.cpp)
target_include_directories(ui PRIVATE
    .
    ../log
    ../audiodev
    ../sysdev
    ../inputdev
    ../app
    ../audio
    ../core
    ../entities
    ../input
    ../level
    ../render
    ../testing)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(ui PRIVATE ../dbg)
target_link_libraries(ui PRIVATE imgui)
