add_library(core OBJECT
    gameboot.cpp
    gamedebug.cpp
    gametick.cpp
    quitforward.cpp
    levelentry.cpp
    rendergameframe.cpp
    tiledebug.cpp)
target_include_directories(core PUBLIC .)

target_link_libraries(core
    PRIVATE sysdev log base prof inputdev audiodev windev dbg input gfx audio script level entities render screens game sim flow imgui renderapi)
