add_library(bot OBJECT
    actions.cpp
    bombmodel.cpp
    botrun.cpp
    botview.cpp
    danger.cpp
    explore.cpp
    foemodel.cpp
    motor.cpp
    nav.cpp
    planner.cpp
    policy.cpp
    route.cpp
    script.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi
    PRIVATE sysdev log base input level entities game)
