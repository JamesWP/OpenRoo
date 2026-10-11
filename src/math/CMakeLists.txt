add_library(math OBJECT
    splinepath.cpp
    vecmath.cpp)
target_include_directories(math PUBLIC .)
target_link_libraries(math
    PRIVATE sysdev log
    PUBLIC renderapi)
