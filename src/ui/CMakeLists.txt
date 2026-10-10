add_library(ui OBJECT
    highscores.cpp
    menutree.cpp
    saveslots.cpp
    scriptplayer.cpp
    textentry.cpp)
target_include_directories(ui PRIVATE
    ../fx
    ../base
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
    ../gfx
    ../testing)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(ui PRIVATE
    ../fx ../dbg)
target_link_libraries(ui PRIVATE imgui)
