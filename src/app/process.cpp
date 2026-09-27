#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "veh.h"
#include "launcher.h"

#include "gamestate.h"
#include "process.h"

/* KAROO_CRT_DIAG=1 logs the standard-error handle and what a write to stderr
 * returns.  This is a GUI process with no console, so anything the game writes
 * to stderr goes nowhere; the probe shows where it would go. */
static void crt_stderr_probe(void)
{
    char buf[16];
    if (GetEnvironmentVariableA("KAROO_CRT_DIAG", buf, sizeof(buf)) == 0 ||
        buf[0] == '0')
        return;

    static const char msg[] = "karoo: CRT_PLAN Stage B stderr probe\n";
    const unsigned n = (unsigned)fwrite(msg, sizeof(msg) - 1, 1, stderr);

    log_write("crt: STD_ERROR_HANDLE=%p  our fwrite(stderr)=%u\n",
              (void *)GetStdHandle(STD_ERROR_HANDLE), n);
}

void Process_Attach(const char *log_name)
{
    log_open(log_name);
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    log_write("karoo_hooks loaded by: %s\n", path);
    install_veh();
    crt_stderr_probe();
    launcher_init();
}

void Process_Detach(void)
{
    gamestate_dump("process-exit");
    log_close();
}
