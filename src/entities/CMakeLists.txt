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
    .)

target_link_libraries(entities PRIVATE imgui)
