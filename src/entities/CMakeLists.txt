add_library(entities OBJECT
    bomb.cpp
    fallingtile.cpp
    bridgeobject.cpp
    entitymath.cpp
    foe.cpp
    foepath.cpp
    liftobject.cpp
    movableentity.cpp
    objectremove.cpp
    player.cpp
    platformobject.cpp
    splinepath.cpp
    uvanimator.cpp)
target_include_directories(entities PRIVATE
    ../fx
    ../base
    .
    ../log
    ../audiodev
    ../app
    ../audio
    ../core
    ../level
    ../render
    ../gfx
    ../ui
    ../sysdev)

# The debug UI sections and map layers drawn from this group (dbg.h).
target_include_directories(entities PRIVATE
    ../fx ../dbg)
target_link_libraries(entities PRIVATE imgui)
