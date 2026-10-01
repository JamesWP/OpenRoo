add_library(testing OBJECT
    plan.cpp
    policy.cpp
    record.cpp)
target_include_directories(testing PRIVATE
    .
    ../audiodev
    ../app
    ../audio
    ../core
    ../entities
    ../input
    ../level
    ../render
    ../ui)
