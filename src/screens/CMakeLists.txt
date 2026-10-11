add_library(screens OBJECT
    hud.cpp
    levelselectview.cpp
    menuscreens.cpp
    scoreoverlay.cpp)
target_include_directories(screens PUBLIC .)
target_link_libraries(screens
    PUBLIC  gfx session
    PRIVATE sysdev log base math inputdev input ui script level entities render)
