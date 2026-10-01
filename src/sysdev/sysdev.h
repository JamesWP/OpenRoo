/* The platform system layer: clocks, the local date and time, the executable's
 * path and the crash logger.  The only code that may call the system's own
 * timing, time-of-day and exception APIs; the header is opaque and free of
 * platform headers, so a port replaces the .cpp files beside it. */
#pragma once

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

/* The running executable's path into buf; false if it does not fit or is
 * unknown. */
bool executablePath(char *buf, unsigned size);

/* Logs access violations in the process's own code (registers and the top of
 * the stack) and guard-page faults through the log function, then lets the
 * exception continue to the normal handlers. */
void installCrashLogger();

}  // namespace sysdev
