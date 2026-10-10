add_library(level OBJECT
    extraobjects.cpp
    gridrestore.cpp
    levelmap.cpp
    tilenames.cpp
    tilequery.cpp)
target_include_directories(level PUBLIC .)

target_link_libraries(level
    PUBLIC  gfx
    PRIVATE sysdev log base imgui)
