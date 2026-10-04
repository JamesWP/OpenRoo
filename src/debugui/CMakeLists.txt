# The in-game debug UI: Dear ImGui drawn through RenderDevice (its renderer
# half) and over windev (its platform half).  Off unless KAROO_DEBUGUI=1; F10
# shows and hides it.  Nothing in the game depends on it but src/app, which
# starts it.
add_library(debugui OBJECT
    debugui.cpp)
target_include_directories(debugui PRIVATE
    .
    ../log
    ../render
    ../sysdev
    ../windev)
target_link_libraries(debugui PRIVATE imgui)
