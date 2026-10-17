add_library(fx OBJECT
    explodedebris.cpp
    generators.cpp
    particles.cpp)
target_include_directories(fx PUBLIC .)
target_link_libraries(fx
    PUBLIC  gfx renderapi
    PRIVATE sysdev log base)
