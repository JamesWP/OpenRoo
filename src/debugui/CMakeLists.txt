# The debug UI's backend: Dear ImGui drawn through RenderDevice (its renderer
# half) and over windev (its platform half).  Hidden until F10 shows it.  The
# content is drawn by the game's own code through src/dbg; nothing in the game
# depends on this group but src/app, which starts it.
add_library(debugui OBJECT
    debugui.cpp)
target_include_directories(debugui PRIVATE
    ../base
    ../dbg
    ../prof
    .
    ../audiodev
    ../audio
    ../inputdev
    ../input
    ../videodev
    ../gl
    ../app
    ../core
    ../entities
    ../ui
    ../level
    ../log
    ../render
    ../gfx
    ../sysdev
    ../windev)
target_link_libraries(debugui PRIVATE imgui)
