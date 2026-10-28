add_library(game OBJECT
    game.cpp
    soundevents.cpp)
target_include_directories(game PUBLIC .)
target_link_libraries(game
    PUBLIC  base audio ui camera script session entities
    PRIVATE soundapi log inputapi audiodev windev level)
