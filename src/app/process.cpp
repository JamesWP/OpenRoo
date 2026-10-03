#include <stdio.h>
#include <iostream>
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
    std::cerr.write(msg, sizeof(msg) - 1);

    g_logger.write("crt: cerr write ok=%d\n", (int)(bool)std::cerr);
}

void Process_Attach(const char *log_name)
{
    g_logger.open(log_name);
    g_logger.write("karoo_hooks loaded from: %s\n", sysdev::executableDir().c_str());
    sysdev::setLog(log_sink);
    crt_stderr_probe();
    launcher_init();
}

void Process_Detach(void)
{
    gamestate_dump("process-exit");
}
