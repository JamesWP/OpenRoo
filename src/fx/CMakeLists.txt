add_library(fx OBJECT
    explodedebris.cpp
    factory.cpp
    generators.cpp
    particles.cpp)
target_include_directories(fx PRIVATE
    .
    ../base
    ../log
    ../sysdev
    ../gfx)
