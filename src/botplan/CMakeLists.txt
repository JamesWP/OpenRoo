add_library(botplan OBJECT
    board.cpp
    goap.cpp)
target_include_directories(botplan PUBLIC .)
target_link_libraries(botplan
    PUBLIC botapi botmodel
    PRIVATE level)
