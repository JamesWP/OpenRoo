add_library(level OBJECT
    extraobjects.cpp
    gridrestore.cpp
    levelmap.cpp
    levelentry.cpp
    levelparse.cpp
    levelplacements.cpp
    levelscore.cpp
    levelsetup.cpp
    levelsounds.cpp
    tiledebug.cpp
    tilequery.cpp)
target_include_directories(level PRIVATE
    ../fx
    ../base
    ../inputdev
    .
    ../log
    ../audiodev
    ../windev
    ../app
    ../audio
    ../core
    ../entities
    ../render
    ../gfx
    ../ui
    ../sysdev)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(level PRIVATE
    ../fx ../dbg)
target_link_libraries(level PRIVATE imgui)
