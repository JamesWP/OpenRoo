add_library(testing OBJECT
    record.cpp)
target_include_directories(testing PUBLIC .)
target_link_libraries(testing
    PRIVATE sysdev log base inputdev)
