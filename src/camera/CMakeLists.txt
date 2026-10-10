add_library(camera OBJECT
    scriptplayer.cpp)
target_include_directories(camera PUBLIC .)
target_link_libraries(camera
    PUBLIC  math audiodev
    PRIVATE sysdev log audio)
