# The rendering interface (renderdevice.h, with the types it speaks in): game
# code draws through it, and gl implements it.  Header-only, so an INTERFACE
# library; it depends on nothing in the project.
add_library(renderapi INTERFACE)
target_include_directories(renderapi INTERFACE .)
