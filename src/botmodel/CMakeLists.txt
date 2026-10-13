add_library(botmodel OBJECT
    bombmodel.cpp
    danger.cpp
    distances.cpp
    fallingmodel.cpp
    foemodel.cpp
    nav.cpp
    route.cpp)
target_include_directories(botmodel PUBLIC .)
target_link_libraries(botmodel
    PUBLIC botapi
    PRIVATE level entities)
