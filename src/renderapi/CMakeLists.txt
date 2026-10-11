# The rendering interface (renderdevice.h, with the types it speaks in, and
# renderstates.h, single-field helpers over it): game code draws through
# it, and gl implements it.  Header-only, so an INTERFACE library; it depends on nothing in the project.
add_library(renderapi INTERFACE)
target_include_directories(renderapi INTERFACE .)
