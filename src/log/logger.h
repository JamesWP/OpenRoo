#pragma once

/* The one log: karoo_hooks.log in the run directory.  Everything we and the
 * game's own code report goes through the single global g_logger, one line
 * each, prefixed with the tick count and flushed as it is written. */

#include <stdarg.h>

class Logger {
public:
    /* Writes nothing until open; closes any earlier file. */
    void open(const char *path);
    void close();

    /* A line from our own code. */
    void write(const char *fmt, ...);

    /* The game's messages.  A message logs only if its level is at least 1;
     * level 0 is off. */
    void logMessage(int level, const char *fmt, ...);

    /* As logMessage, tagged "File: <file>, Line: <line>: ". */
    void logSourceLocation(int level, const char *file, int line,
                           const char *fmt, ...);

private:
    friend void log_sink(const char *fmt, ...);
    void emit(const char *prefix, const char *fmt, va_list ap);

    /* A plain FILE (opaque here), never closed at exit, so the log still
     * works from the destructors of other globals whatever order they run
     * in. */
    void *fp_ = nullptr;
};

extern Logger g_logger;

/* g_logger.write as a plain function, for the platform layers' log hooks. */
void log_sink(const char *fmt, ...);
