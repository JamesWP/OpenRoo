add_library(simbot OBJECT
    actions.cpp
    planner.cpp
    moves.cpp
    reach.cpp
    simbot.cpp)
target_include_directories(simbot PUBLIC .)
target_link_libraries(simbot
    PUBLIC botapi
    PRIVATE entities level log)
