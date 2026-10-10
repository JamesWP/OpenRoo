add_library(ui OBJECT
    highscores.cpp
    menutree.cpp
    saveslots.cpp
    textentry.cpp)
target_include_directories(ui PUBLIC .)

target_link_libraries(ui
    PUBLIC  inputdev
    PRIVATE sysdev log base testing imgui)
