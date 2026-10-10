add_library(screens OBJECT
    levelselect.cpp
    menunav.cpp
    menuscreens.cpp
    scoreoverlay.cpp)
target_include_directories(screens PRIVATE
    .)
