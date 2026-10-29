add_library(core OBJECT
    gameboot.cpp
    entitydebug.cpp
    gamedebug.cpp
    gametick.cpp
    quitforward.cpp
    levelentry.cpp
    frame.cpp
    tiledebug.cpp)
target_include_directories(core PUBLIC .)

target_link_libraries(core
    PUBLIC  sequencer
    PRIVATE sysdev log base prof inputapi audiodev windev dbg input gfx audio script level entities render game sim drawhelpers imgui renderapi)
