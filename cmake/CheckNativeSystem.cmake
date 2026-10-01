# Fails if a file outside the platform groups (src/windev, audiodev, inputdev,
# videodev, sysdev) calls the system clock, time-of-day, module path or
# exception APIs (include paths cannot hide those).
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(file IN LISTS files)
    if(file MATCHES "/src/(windev|audiodev|inputdev|videodev|sysdev)/")
        continue()
    endif()
    file(STRINGS "${file}" bad REGEX "(^|[^A-Za-z_])(QueryPerformanceCounter|QueryPerformanceFrequency|GetTickCount|GetTickCount64|timeGetTime|timeBeginPeriod|GetLocalTime|GetSystemTime|GetModuleFileName[AW]?|AddVectoredExceptionHandler|SetUnhandledExceptionFilter)[ \t]*[(]")
    if(bad)
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        message(SEND_ERROR "${rel}: system clock/time/exception API outside the platform groups (${bad}); use sysdev.h")
    endif()
endforeach()
