# The input interfaces (keysource.h) and the key codes (keycodes.h): what the
# game reads keys through, and what a group that supplies keys implements.
# inputdev, testing and any further source implement them; input collects the
# sources.  Header-only, so an INTERFACE library; it depends on nothing in the
# project.
add_library(inputapi INTERFACE)
target_include_directories(inputapi INTERFACE .)
