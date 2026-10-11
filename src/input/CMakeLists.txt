add_library(input OBJECT
    keys.cpp
    progctrl.cpp)
target_include_directories(input PUBLIC .)
target_link_libraries(input
    PUBLIC  inputapi
    PRIVATE inputdev sysdev log base)
