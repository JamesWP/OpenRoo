add_library(audio OBJECT
    cdm.cpp
    cdthemes.cpp
    doublesoundbuff.cpp
    fixedsounds.cpp
    movie.cpp
    soundmanager.cpp
    soundobj.cpp
    voicepool.cpp)
target_include_directories(audio PRIVATE
    .
    ../audiodev
    ../app
    ../core
    ../entities
    ../level
    ../render
    ../testing
    ../ui)
