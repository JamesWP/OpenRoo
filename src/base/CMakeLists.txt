add_library(base OBJECT
    config.cpp
    ini.cpp)
target_include_directories(base PRIVATE
    .
    ../log
    ../sysdev)
