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

unsigned getEnv(const char *name, char *buf, unsigned size)
{
    return GetEnvironmentVariableA(name, buf, size);
}

std::string getEnv(const char *name)
{
    unsigned n = getEnv(name, NULL, 0);
    if (n == 0)
        return std::string();
    std::string value(n, '\0');
    n = getEnv(name, &value[0], n);
    value.resize(n < value.size() ? n : 0);
    return value;
}

std::string executablePath()
{
    std::string path(MAX_PATH, '\0');
    for (;;) {
        DWORD n = GetModuleFileNameA(NULL, &path[0], (DWORD)path.size());
        if (n == 0)
            return std::string();
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

}  // namespace sysdev
