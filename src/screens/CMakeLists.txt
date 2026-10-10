add_library(screens OBJECT
    hud.cpp
    levelselect.cpp
    menunav.cpp
    menuscreens.cpp
    scoreoverlay.cpp)
target_include_directories(screens PUBLIC .)
target_link_libraries(screens
    PUBLIC  gfx session
    PRIVATE sysdev log base math inputdev testing input ui camera level entities render)
