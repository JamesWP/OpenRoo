add_library(ui OBJECT
    highscores.cpp
    menutree.cpp
    saveslots.cpp
    textentry.cpp)
target_include_directories(ui PRIVATE
    .)

target_link_libraries(ui PRIVATE imgui)
