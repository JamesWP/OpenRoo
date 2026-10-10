# The central logging library: the single global logger.
add_library(log OBJECT
    logger.cpp)
target_include_directories(log PRIVATE
    .)
