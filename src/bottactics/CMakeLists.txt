add_library(bottactics OBJECT
    actions.cpp
    ambush.cpp
    blast.cpp
    bombs.cpp
    collect.cpp
    drops.cpp
    exit.cpp
    option.cpp
    switch.cpp
    tactics.cpp
    walk.cpp)
target_include_directories(bottactics PUBLIC .)
target_link_libraries(bottactics
    PUBLIC botmodel botmotor botplan)
