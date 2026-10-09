add_library(level OBJECT
    extraobjects.cpp
    gridrestore.cpp
    levelentry.cpp
    levelmap.cpp
    levelparse.cpp
    levelplacements.cpp
    levelscore.cpp
    levelsetup.cpp
    levelsounds.cpp
    tilequery.cpp)
target_include_directories(level PRIVATE
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
    ../ui
    ../sysdev)
