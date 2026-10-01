# Fails if a file outside the platform groups (src/windev, audiodev, inputdev, videodev) calls the system window, message loop,
# message box or dialog APIs (include paths cannot hide those).
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/(windev|audiodev|inputdev|videodev)/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "(^|[^A-Za-z_])(RegisterClass[AW]?|PeekMessage[AW]?|GetMessage[AW]?|DispatchMessage[AW]?|DefWindowProc[AW]?|PostQuitMessage|MessageBox[AW]?|DialogBox[A-Za-z]*|EndDialog|EnumWindows|PostMessage[AW]?|SendMessage[AW]?|DestroyWindow)[ \t]*[(]")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: system window API outside src/windev/ (${bad}); use windev.h")
    endif()
endforeach()
