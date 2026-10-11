add_library(game OBJECT
    game.cpp)
target_include_directories(game PUBLIC .)
target_link_libraries(game
    PUBLIC  base audio ui camera script session entities
    PRIVATE log inputapi audiodev windev level)
