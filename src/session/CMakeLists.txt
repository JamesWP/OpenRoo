add_library(session OBJECT
    levelthemes.cpp
    session.cpp)
target_include_directories(session PUBLIC .)
target_link_libraries(session
    PRIVATE sysdev log base)
