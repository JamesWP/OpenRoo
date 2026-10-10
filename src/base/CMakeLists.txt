add_library(base OBJECT
    clock.cpp
    config.cpp
    gametime.cpp
    ini.cpp
    themesounds.cpp)
target_include_directories(base PRIVATE
    .
    ../log
    ../sysdev)
