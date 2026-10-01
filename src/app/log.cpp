#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "log.h"
#include "sysdev.h"

static FILE *g_log = NULL;

void log_open(const char *path)
{
    g_log = fopen(path, "w");
}

void log_close(void)
{
    if (g_log) { fclose(g_log); g_log = NULL; }
}

void log_write(const char *fmt, ...)
{
    if (!g_log) return;
    fprintf(g_log, "[%u] ", sysdev::tickMs());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}
