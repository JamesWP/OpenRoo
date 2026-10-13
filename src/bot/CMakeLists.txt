add_library(bot OBJECT
    botrun.cpp
    botview.cpp
    explore.cpp
    motor.cpp
    nav.cpp
    policy.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi
    PRIVATE sysdev log base input level entities game)
