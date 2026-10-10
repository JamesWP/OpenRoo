add_library(core OBJECT
    gameboot.cpp
    gamedebug.cpp
    gametick.cpp
    levelentry.cpp
    rendergameframe.cpp
    tiledebug.cpp)
target_include_directories(core PRIVATE
    .)

target_link_libraries(core PRIVATE imgui)
