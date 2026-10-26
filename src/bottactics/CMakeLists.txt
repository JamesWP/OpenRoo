add_library(bottactics OBJECT
    actions.cpp
    ambush.cpp
    await.cpp
    bait.cpp
    blast.cpp
    bombs.cpp
    collect.cpp
    cross.cpp
    drops.cpp
    exit.cpp
    freeze.cpp
    goad.cpp
    send.cpp
    throwers.cpp
    lure.cpp
    drop.cpp
    glide.cpp
    glider.cpp
    moves.cpp
    option.cpp
    playout.cpp
    release.cpp
    switch.cpp
    decisionlog.cpp
    deliberate.cpp
    fallbacks.cpp
    senses.cpp
    tactics.cpp
    variants.cpp
    walk.cpp)
target_include_directories(bottactics PUBLIC .)
target_link_libraries(bottactics
    PUBLIC botmodel botmotor botplan
    PRIVATE base)
