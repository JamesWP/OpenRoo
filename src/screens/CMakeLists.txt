add_library(screens OBJECT
    hud.cpp
    levelselect.cpp
    menunav.cpp
    menuscreens.cpp
    scoreoverlay.cpp)
target_include_directories(screens PRIVATE
    .)
