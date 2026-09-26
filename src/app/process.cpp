#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "veh.h"
#include "launcher.h"

#include "gamestate.h"
#include "process.h"


/* KAROO_CRT_DIAG=1 -- the census behind CRT_PLAN.md Stage B.
 *
 * Stage B moved six log writers off the game's fwrite and onto our own, on
 * the finding that 0x00469cf8 is `stderr` (_iob[2]), not a log file.  The
 * change cannot be observed by watching output, and the reason is itself
 * worth recording: this is a GUI-subsystem process, so it has no stderr
 * handle at all and every one of those log lines has always been discarded
 * -- before this change as much as after.  Proton's steam-123456.log
 * captures Wine's own channels, not the app's fd 2, so a write there
 * surfaces nowhere.
 *
 * What can be checked is that our stream and the game's are the same
 * stream, so this logs, to karoo_hooks.log:
 *
 *   - GetStdHandle(STD_ERROR_HANDLE), the OS handle both CRTs bind fd 2 to;
 *   - the game's _iob[2] _flag/_file, read live out of its CRT data, which
 *     should be _IOWRT (0x2) and 2;
 *   - what our own fwrite to stderr returns.
 *
 * Deliberately not a KAROO_CRT_FX: a control perturbs behaviour a gate can
 * see, and there is none here.  Calling it one would overstate it. */
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
