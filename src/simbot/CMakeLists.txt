add_library(simbot OBJECT
    actions.cpp
    collect.cpp
    exit.cpp
    facts.cpp
    subgoals.cpp
    kill.cpp
    routeattempt.cpp
    movegraph.cpp
    pickup.cpp
    moves.cpp
    planner.cpp
    gliding.cpp
    routing.cpp
    schedule.cpp
    simbot.cpp
    simlog.cpp
    survey.cpp
    trap.cpp)
target_include_directories(simbot PUBLIC .)
target_link_libraries(simbot
    PUBLIC botapi
    PRIVATE entities level log sysdev)
