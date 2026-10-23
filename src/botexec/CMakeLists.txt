add_library(botexec OBJECT
    executive.cpp)
target_include_directories(botexec PUBLIC .)
target_link_libraries(botexec
    PUBLIC botapi botmodel botmotor botplan bottactics
    PRIVATE sysdev)
