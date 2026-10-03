#include <SDL3/SDL.h>
#include "sysdev.h"

namespace sysdev {

LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

unsigned tickMs()
{
    return (unsigned)SDL_GetTicks();
}

unsigned timerMs()
{
    return (unsigned)SDL_GetTicks();
}

unsigned long long perfCounter()
{
    return SDL_GetPerformanceCounter();
}

unsigned long long perfFrequency()
{
    return SDL_GetPerformanceFrequency();
}

LocalTime localTime()
{
    LocalTime t = { 1970, 1, 1, 0, 0, 0 };
    SDL_Time now;
    SDL_DateTime dt;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true)) {
        t.year = dt.year;   t.month  = dt.month;  t.day    = dt.day;
        t.hour = dt.hour;   t.minute = dt.minute; t.second = dt.second;
    }
    return t;
}

unsigned getEnv(const char *name, char *buf, unsigned size)
{
    const char *value = SDL_getenv(name);
    if (!value || !*value)
        return 0;
    unsigned n = (unsigned)SDL_strlen(value);
    if (n >= size)
        return n + 1;
    SDL_memcpy(buf, value, n + 1);
    return n;
}

std::string getEnv(const char *name)
{
    const char *value = SDL_getenv(name);
    return value ? value : "";
}

std::string executableDir()
{
    const char *dir = SDL_GetBasePath();
    return dir ? dir : "";
}

}  // namespace sysdev
