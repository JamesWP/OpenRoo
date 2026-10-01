add_library(ui OBJECT
    cheatcode.cpp
    highscores.cpp
    levelselect.cpp
    menu.cpp
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
