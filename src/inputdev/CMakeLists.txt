# The only group that may include the system input headers (DirectInput) or
# poll keys.  It depends on nothing else in the project.
add_library(inputdev OBJECT
    directinput.cpp)
target_include_directories(inputdev PRIVATE .)
target_link_libraries(inputdev PRIVATE dinput8 dxguid)
