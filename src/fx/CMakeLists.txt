add_library(fx OBJECT
    explodedebris.cpp
    generators.cpp
    particles.cpp)
target_include_directories(fx PUBLIC .)
target_link_libraries(fx
    PRIVATE gfx sysdev log base renderapi)
