# The debug UI's shared frame: the ImGui frame that opens at the top of the
# game's frame and closes when the overlay draws it, the main window the
# components add their sections to, and the Map window they draw layers on.
# The components (level, entities, core, ui, prof) draw their own parts from
# their own code; the backend that puts the result on screen is debugui.
add_library(dbg OBJECT
    dbg.cpp)
target_include_directories(dbg PRIVATE
    .
    ../log
    ../prof
    ../windev)
target_link_libraries(dbg PRIVATE imgui)
