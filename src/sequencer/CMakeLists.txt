# The order things happen in: the frame's draw passes, and what each game
# state does with the keys.  It decides the order and calls; the logic it
# calls lives in the groups below.
add_library(sequencer OBJECT
    camerainput.cpp
    cheatcode.cpp
    drawframe.cpp
    inputsetup.cpp
    keypress.cpp
    levelselect.cpp
    playflow.cpp
    scoretally.cpp)
target_include_directories(sequencer PUBLIC .)
target_link_libraries(sequencer
    PRIVATE sysdev log base prof inputapi audiodev windev input audio ui level entities session game sim
            gfx renderapi drawhelpers render screens)
