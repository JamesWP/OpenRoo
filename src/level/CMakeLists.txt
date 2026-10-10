add_library(level OBJECT
    extraobjects.cpp
    gridrestore.cpp
    levelmap.cpp
    tilenames.cpp
    tilequery.cpp)
target_include_directories(level PRIVATE
    .)

target_link_libraries(level PRIVATE imgui)
