# Fails if a file outside src/d3d/ includes a system Direct3D header
# (include paths cannot hide those).
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/d3d/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "^[ \t]*#[ \t]*include[ \t]*<(d3d|ddraw|dxgi)")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: Direct3D headers outside src/d3d/ (${bad}); use renderdevice.h")
    endif()
endforeach()
