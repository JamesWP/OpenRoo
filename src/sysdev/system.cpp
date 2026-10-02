#include <windows.h>
#include <timeapi.h>
#include "sysdev.h"

namespace sysdev {

LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

unsigned tickMs()
{
    return GetTickCount();
}

unsigned timerMs()
{
    return timeGetTime();
}

unsigned long long perfCounter()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (unsigned long long)now.QuadPart;
}

unsigned long long perfFrequency()
{
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq))
        freq.QuadPart = 1000;
    return (unsigned long long)freq.QuadPart;
}

LocalTime localTime()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    LocalTime t = { st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond };
    return t;
}

bool executablePath(char *buf, unsigned size)
{
    DWORD n = GetModuleFileNameA(NULL, buf, size);
    return n > 0 && n < size;
}

unsigned getEnv(const char *name, char *buf, unsigned size)
{
    return GetEnvironmentVariableA(name, buf, size);
}

}  // namespace sysdev
