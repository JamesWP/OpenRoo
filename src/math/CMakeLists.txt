add_library(math OBJECT
    splinepath.cpp
    vecmath.cpp)
target_include_directories(math PRIVATE
    .)
