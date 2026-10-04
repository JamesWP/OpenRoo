# Fails if a file outside src/gl/ includes a system OpenGL or Direct3D header
# (include paths cannot hide those).
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/gl/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "^[ \t]*#[ \t]*include[ \t]*<(GL/|GLES|gl[0-9a-z]*\\.h|d3d|ddraw|dxgi|SDL3?/SDL_opengl)")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: graphics API headers outside src/gl/ (${bad}); use renderdevice.h")
    endif()
endforeach()
