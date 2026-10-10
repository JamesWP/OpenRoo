# The sound interfaces (soundvoice.h): game objects hold and play sounds
# through them, and audiodev and audio implement them.  Header-only, so an
# INTERFACE library; it depends on nothing in the project.
add_library(soundapi INTERFACE)
target_include_directories(soundapi INTERFACE .)
