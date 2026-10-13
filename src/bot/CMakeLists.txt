add_library(bot OBJECT
    botrun.cpp
    botview.cpp
    motor.cpp
    policy.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi
    PRIVATE sysdev log base input level entities game)
