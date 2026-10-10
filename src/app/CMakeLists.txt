add_library(app OBJECT
    launcher.cpp
    launcherdialogs.cpp
    main.cpp
    process.cpp)
target_include_directories(app PUBLIC .)
target_link_libraries(app
    PRIVATE sysdev log base prof inputdev audiodev videodev windev testing input gfx audio render screens game sim flow core debugui)

