add_library(simbot OBJECT
    actions.cpp
    collect.cpp
    exit.cpp
    goto.cpp
    movegraph.cpp
    moves.cpp
    planner.cpp
    routing.cpp
    simbot.cpp
    simlog.cpp
    survey.cpp)
target_include_directories(simbot PUBLIC .)
target_link_libraries(simbot
    PUBLIC botapi
    PRIVATE entities level log sysdev)
