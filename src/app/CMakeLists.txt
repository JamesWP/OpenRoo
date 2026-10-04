add_library(app OBJECT
    config.cpp
    launcher.cpp
    launcherdialogs.cpp
    main.cpp
    process.cpp)
target_include_directories(app PRIVATE
    .
    ../log
    ../audiodev
    ../sysdev
    ../inputdev
    ../videodev
    ../windev
    ../audio
    ../core
    ../d3d
    ../entities
    ../input
    ../level
    ../render
    ../testing
    ../ui)

