add_library(botplan OBJECT
    board.cpp
    worlds.cpp
    goap.cpp
    goals.cpp)
target_include_directories(botplan PUBLIC .)
target_link_libraries(botplan
    PUBLIC botapi botmodel
    PRIVATE level)
