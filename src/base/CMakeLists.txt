add_library(base OBJECT
    clock.cpp
    config.cpp
    gametime.cpp
    ini.cpp)
target_include_directories(base PUBLIC .)
target_link_libraries(base
    PRIVATE sysdev log
    PUBLIC renderapi)
