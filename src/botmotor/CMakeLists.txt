add_library(botmotor OBJECT
    motor.cpp
    script.cpp)
target_include_directories(botmotor PUBLIC .)
target_link_libraries(botmotor
    PUBLIC botapi
    PRIVATE level)
