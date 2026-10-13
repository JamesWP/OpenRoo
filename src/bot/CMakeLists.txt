add_library(bot OBJECT
    actions.cpp
    botrun.cpp
    capture.cpp
    explore.cpp
    planner.cpp
    policies.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi botapi botmodel botmotor
    PRIVATE sysdev log base input level entities game)
