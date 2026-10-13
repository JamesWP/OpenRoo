add_library(bot OBJECT
    bombmodel.cpp
    botrun.cpp
    botview.cpp
    collect.cpp
    danger.cpp
    explore.cpp
    foemodel.cpp
    motor.cpp
    nav.cpp
    policy.cpp
    route.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi
    PRIVATE sysdev log base input level entities game)
