add_library(entities OBJECT
    bomb.cpp
    fallingtile.cpp
    bridgeobject.cpp
    bridgesurf.cpp
    checkpoint.cpp
    entitymath.cpp
    explodedebris.cpp
    factory.cpp
    foe.cpp
    foepath.cpp
    framepose.cpp
    generators.cpp
    liftobject.cpp
    movableentity.cpp
    objectremove.cpp
    player.cpp
    platformobject.cpp
    splinepath.cpp
    uvanimator.cpp)
target_include_directories(entities PRIVATE
    .
    ../log
    ../audiodev
    ../app
    ../audio
    ../core
    ../level
    ../render
    ../ui
    ../sysdev)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(entities PRIVATE ../dbg)
target_link_libraries(entities PRIVATE imgui)
