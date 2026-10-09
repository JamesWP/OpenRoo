add_library(base OBJECT
    ini.cpp)
target_include_directories(base PRIVATE
    .
    ../sysdev)
