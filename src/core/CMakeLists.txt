add_library(core OBJECT
    gameboot.cpp
    gamedebug.cpp
    gametick.cpp
    quitforward.cpp
    levelentry.cpp
    frame.cpp
    drawframe.cpp
    tiledebug.cpp)
target_include_directories(core PUBLIC .)

target_link_libraries(core
    PRIVATE sysdev log base prof inputapi audiodev windev dbg input gfx audio script level entities render screens game sim flow imgui renderapi)
