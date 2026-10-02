#include <windows.h>
#include <stdio.h>
#include "logger.h"
#include "sysdev.h"
#include "launcher.h"

#include "gamestate.h"
#include "process.h"

/* KAROO_CRT_DIAG=1 logs what a write to stderr returns.  This is a GUI process
 * with no console, so anything the game writes to stderr goes nowhere. */
static void crt_stderr_probe(void)
{
    char buf[16];
    if (sysdev::getEnv("KAROO_CRT_DIAG", buf, sizeof(buf)) == 0 ||
        buf[0] == '0')
        return;

    static const char msg[] = "karoo: CRT_PLAN Stage B stderr probe\n";
    const unsigned n = (unsigned)fwrite(msg, sizeof(msg) - 1, 1, stderr);

    g_logger.write("crt: our fwrite(stderr)=%u\n", n);
}

void Process_Attach(const char *log_name)
{
    g_logger.open(log_name);
    char path[MAX_PATH];
    if (!sysdev::executablePath(path, MAX_PATH))
        path[0] = '\0';
    g_logger.write("karoo_hooks loaded by: %s\n", path);
    sysdev::setLog(log_sink);
    sysdev::installCrashLogger();
    crt_stderr_probe();
    launcher_init();
}

void Process_Detach(void)
{
    gamestate_dump("process-exit");
}
