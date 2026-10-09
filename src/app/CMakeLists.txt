add_library(app OBJECT
    launcher.cpp
    launcherdialogs.cpp
    main.cpp
    process.cpp)
target_include_directories(app PRIVATE
    ../base
    .
    ../log
    ../prof
    ../audiodev
    ../sysdev
    ../inputdev
    ../videodev
    ../windev
    ../audio
    ../core
    ../debugui
    ../gl
    ../entities
    ../input
    ../level
    ../render
    ../gfx
    ../testing
    ../ui)

