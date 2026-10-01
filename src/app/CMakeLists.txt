add_library(app OBJECT
    config.cpp
    gamelog.cpp
    launcher.cpp
    launcherdialogs.cpp
    log.cpp
    main.cpp
    process.cpp
    veh.cpp)
target_include_directories(app PRIVATE
    .
    ../audiodev
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

target_link_libraries(app PRIVATE winmm)
