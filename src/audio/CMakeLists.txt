add_library(audio OBJECT
    cdm.cpp
    cdthemes.cpp
    cfaktsound.cpp
    doublesoundbuff.cpp
    fixedsounds.cpp
    movie.cpp
    soundmanager.cpp
    soundobj.cpp
    static.cpp
    voicepool.cpp)
target_include_directories(audio PRIVATE
    .
    ../app
    ../core
    ../entities
    ../level
    ../render
    ../testing
    ../ui)
