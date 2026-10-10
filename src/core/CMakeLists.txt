add_library(core OBJECT
    checkpoint.cpp
    gameboot.cpp
    gamereset.cpp
    levelsetup.cpp
    levelsounds.cpp
    levelentry.cpp
    levelscore.cpp
    levelparse.cpp
    tiledebug.cpp
    gamestate.cpp
    gamedebug.cpp
    gametick.cpp
    keypress.cpp
    inputsetup.cpp
    camerainput.cpp
    rendergameframe.cpp
    cheatcode.cpp
    scoretally.cpp
    fixedsounds.cpp
    soundobj.cpp)
target_include_directories(core PRIVATE
    .)

target_link_libraries(core PRIVATE imgui)
