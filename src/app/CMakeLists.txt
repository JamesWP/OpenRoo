add_library(app OBJECT
    config.cpp
    gamelog.cpp
    launcher.cpp
    launcherdialogs.cpp
    log.cpp
    main.cpp
    process.cpp)
target_include_directories(app PRIVATE
    .
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
    ../ui)

