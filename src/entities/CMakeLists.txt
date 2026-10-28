add_library(entities OBJECT
    bomb.cpp
    fallingtile.cpp
    bridgeobject.cpp
    entitymath.cpp
    foe.cpp
    foepath.cpp
    liftobject.cpp
    movableentity.cpp
    player.cpp
    platformobject.cpp
    world.cpp)
target_include_directories(entities PUBLIC .)

target_link_libraries(entities
    PUBLIC  base level
    PRIVATE sysdev log prof dbg imgui)
