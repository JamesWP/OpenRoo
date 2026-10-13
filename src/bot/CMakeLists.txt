add_library(bot OBJECT
    botrun.cpp
    capture.cpp
    checks.cpp
    explore.cpp
    policies.cpp)
target_include_directories(bot PUBLIC .)
target_link_libraries(bot
    PUBLIC inputapi botapi botmodel
    PRIVATE sysdev log base input level entities game botmotor botexec)
