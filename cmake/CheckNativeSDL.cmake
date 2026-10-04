# Fails if a file outside the platform groups (src/windev, audiodev, inputdev,
# videodev, sysdev) includes an SDL or FFmpeg header, so SDL and FFmpeg types and calls cannot leak
# into game code.  gl is not on the list: the rendering backend gets its
# window, OpenGL context and displays through windev.h.
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/(windev|audiodev|inputdev|videodev|sysdev)/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "^[ \t]*#[ \t]*include[ \t]*<(SDL3?|SDL3_mixer|libav[a-z]*|libsw[a-z]*)/")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: SDL/FFmpeg outside the platform groups (${bad}); use a platform-group API")
    endif()
endforeach()
