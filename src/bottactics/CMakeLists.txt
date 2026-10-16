add_library(bottactics OBJECT
    actions.cpp
    ambush.cpp
    await.cpp
    blast.cpp
    bombs.cpp
    collect.cpp
    drops.cpp
    exit.cpp
    glide.cpp
    glider.cpp
    option.cpp
    release.cpp
    switch.cpp
    tactics.cpp
    walk.cpp)
target_include_directories(bottactics PUBLIC .)
target_link_libraries(bottactics
    PUBLIC botmodel botmotor botplan)
