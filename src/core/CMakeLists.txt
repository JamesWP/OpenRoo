add_library(core OBJECT
    gameboot.cpp
    gamedebug.cpp
    gametick.cpp
    levelentry.cpp
    rendergameframe.cpp
    tiledebug.cpp)
target_include_directories(core PUBLIC .)

target_link_libraries(core
    PRIVATE sysdev log base prof inputdev audiodev windev dbg testing input gfx audio camera level entities render screens game sim flow imgui)
