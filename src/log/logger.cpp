#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "logger.h"
#include "sysdev.h"

Logger g_logger;

static const int MIN_LEVEL = 1;
static const size_t LINE_MAX_BYTES = 3000;

void Logger::open(const char *path)
{
    close();
    fp_ = fopen(path, "w");
}

void Logger::close()
{
    if (fp_) { fclose(fp_); fp_ = NULL; }
}

/* One fwrite per line, so lines from different threads do not interleave. */
void Logger::emit(const char *prefix, const char *fmt, va_list ap)
{
    if (!fp_) return;
    char line[LINE_MAX_BYTES + 2];
    int n = snprintf(line, sizeof(line), "[%u] %s", sysdev::tickMs(), prefix);
    if (n < 0 || (size_t)n >= LINE_MAX_BYTES) n = 0;
    int m = vsnprintf(line + n, LINE_MAX_BYTES - n, fmt, ap);
    if (m < 0) m = 0;
    size_t len = n + m;
    if (len >= LINE_MAX_BYTES) len = LINE_MAX_BYTES - 1;
    if (len == 0 || line[len - 1] != '\n') line[len++] = '\n';
    fwrite(line, 1, len, fp_);
    fflush(fp_);
}

void Logger::write(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
}

void Logger::logMessage(int level, const char *fmt, ...)
{
    if (level < MIN_LEVEL) return;
    va_list ap;
    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
}

void Logger::logSourceLocation(int level, const char *file, int line,
                               const char *fmt, ...)
{
    if (level < MIN_LEVEL) return;
    char tag[256];
    snprintf(tag, sizeof(tag), "File: %s, Line: %d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    emit(tag, fmt, ap);
    va_end(ap);
}

void log_sink(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_logger.emitV(fmt, ap);
    va_end(ap);
}
