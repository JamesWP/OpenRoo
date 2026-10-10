add_library(flow OBJECT
    camerainput.cpp
    cheatcode.cpp
    inputsetup.cpp
    keypress.cpp
    playflow.cpp
    scoretally.cpp)
target_include_directories(flow PRIVATE
    .)
