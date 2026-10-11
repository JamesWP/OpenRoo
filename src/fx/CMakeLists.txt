add_library(fx OBJECT
    explodedebris.cpp
    factory.cpp
    generators.cpp
    particles.cpp)
target_include_directories(fx PUBLIC .)
target_link_libraries(fx
    PUBLIC  gfx
    PRIVATE sysdev log base renderapi)
