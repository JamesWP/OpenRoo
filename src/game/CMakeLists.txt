add_library(game OBJECT
    game.cpp)
target_include_directories(game PUBLIC .)
target_link_libraries(game
    PUBLIC  base audio ui camera session entities
    PRIVATE log inputdev audiodev windev level)
