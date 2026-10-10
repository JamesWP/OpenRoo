add_library(app OBJECT
    launcher.cpp
    launcherdialogs.cpp
    main.cpp
    process.cpp)
target_include_directories(app PRIVATE
    .)

