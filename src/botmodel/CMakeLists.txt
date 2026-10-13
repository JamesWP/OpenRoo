add_library(botmodel OBJECT
    bombmodel.cpp
    danger.cpp
    fallingmodel.cpp
    liftmodel.cpp
    foemodel.cpp
    nav.cpp
    route.cpp
    terrain.cpp)
target_include_directories(botmodel PUBLIC .)
target_link_libraries(botmodel
    PUBLIC botapi
    PRIVATE level entities)
