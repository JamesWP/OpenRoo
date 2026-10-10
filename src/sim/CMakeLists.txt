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
    playtick.cpp
    soundobj.cpp)
target_include_directories(sim PUBLIC .)
target_link_libraries(sim
    PRIVATE sysdev log base prof audiodev audio ui level entities game)
