# Fails if a file outside the platform groups (src/windev, audiodev, inputdev,
# videodev, sysdev) includes an SDL header, so SDL types and calls cannot leak
# into game code.  d3d is not on the list: a backend that needs the window
# gets it through windev.h.
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/(windev|audiodev|inputdev|videodev|sysdev)/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "^[ \t]*#[ \t]*include[ \t]*<SDL3?/")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: SDL outside the platform groups (${bad}); use a platform-group API")
    endif()
endforeach()
