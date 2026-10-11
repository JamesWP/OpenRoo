add_library(script OBJECT
    scriptplayer.cpp)
target_include_directories(script PUBLIC .)
target_link_libraries(script
    PUBLIC  math audiodev
    PRIVATE sysdev log audio)
