add_library(botworld OBJECT
    compare.cpp
    worldsim.cpp)
target_include_directories(botworld PUBLIC .)
target_link_libraries(botworld
    PUBLIC botapi botmotor)
