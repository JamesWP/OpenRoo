add_library(level OBJECT
    extraobjects.cpp
    gridrestore.cpp
    levelentry.cpp
    levelmap.cpp
    levelparse.cpp
    levelplacements.cpp
    levelreport.cpp
    levelscore.cpp
    levelsetup.cpp
    levelsounds.cpp
    reportwriter.cpp
    tilequery.cpp)
target_include_directories(level PRIVATE
    .
    ../audiodev
    ../app
    ../audio
    ../core
    ../entities
    ../render
    ../ui)
