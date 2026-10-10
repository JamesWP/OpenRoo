add_library(flow OBJECT
    camerainput.cpp
    cheatcode.cpp
    inputsetup.cpp
    keypress.cpp
    playflow.cpp
    scoretally.cpp)
target_include_directories(flow PUBLIC .)
target_link_libraries(flow
    PRIVATE sysdev log base prof inputdev audiodev windev testing input audio ui level entities screens game sim)
