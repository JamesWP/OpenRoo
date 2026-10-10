add_library(sim OBJECT
    checkpoint.cpp
    cheats.cpp
    fixedsounds.cpp
    gamereset.cpp
    gamestate.cpp
    levelparse.cpp
    levelscore.cpp
    levelsetup.cpp
    levelsounds.cpp
    soundobj.cpp)
target_include_directories(sim PRIVATE
    .)
