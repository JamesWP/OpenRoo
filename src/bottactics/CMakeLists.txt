add_library(bottactics OBJECT
    drops.cpp
    tactics.cpp
    walk.cpp)
target_include_directories(bottactics PUBLIC .)
target_link_libraries(bottactics
    PUBLIC botmodel botmotor botplan)
