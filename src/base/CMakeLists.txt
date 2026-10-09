add_library(base OBJECT
    config.cpp
    gamedir.cpp
    ini.cpp)
target_include_directories(base PRIVATE
    .
    ../log
    ../sysdev)
