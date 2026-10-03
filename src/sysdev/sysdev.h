/* The platform system layer: clocks, the local date and time, the executable's
 * directory and the crash logger.  The only code that may call the system's own
 * timing, time-of-day and exception APIs; the header is opaque and free of
 * platform headers, so a port replaces the .cpp files beside it. */
#pragma once

#include <string>

namespace sysdev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* Milliseconds since the system started; wraps after about 49 days. */
unsigned tickMs();

/* Milliseconds from the multimedia timer: the same scale as tickMs but a
 * finer resolution where the system offers one. */
unsigned timerMs();

/* A high-resolution counter and its ticks per second. */
unsigned long long perfCounter();
unsigned long long perfFrequency();

struct LocalTime {
    int year, month, day, hour, minute, second;
};
LocalTime localTime();

/* Copies the environment variable name into buf.  Returns its length without
 * the terminator, or 0 if it is unset; if buf is too small, returns the size
 * needed including the terminator and leaves buf unspecified. */
unsigned getEnv(const char *name, char *buf, unsigned size);

/* The environment variable name, or an empty string if it is unset or empty. */
std::string getEnv(const char *name);

/* The directory the executable lives in, with a trailing separator, or an
 * empty string if it is unknown. */
std::string executableDir();

}  // namespace sysdev
