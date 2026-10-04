add_library(audio OBJECT
    cdm.cpp
    cdthemes.cpp
    doublesoundbuff.cpp
    fixedsounds.cpp
    soundmanager.cpp
    soundobj.cpp
    voicepool.cpp)
target_include_directories(audio PRIVATE
    ../inputdev
    .
    ../log
    ../audiodev
    ../app
    ../core
    ../entities
    ../level
    ../render
    ../testing
    ../ui
    ../sysdev)
