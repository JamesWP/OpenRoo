#include <windows.h>
#include <stdlib.h>
#include "log.h"
#include "windev.h"
#include "launcher.h"

static int   g_skip_launcher    = 0;
static DWORD g_auto_exit_s = 0;

void launcher_end_run(const char *why)
{
    static bool done = false;
    if (done) return;
    done = true;

    log_write("launcher: ending run (%s)\n", why);
    windev::requestClose();
}

void launcher_init(void)
{
    char buf[32];
    if (GetEnvironmentVariableA("KAROO_SKIP_LAUNCHER", buf, sizeof(buf)))
        g_skip_launcher = atoi(buf);
    if (GetEnvironmentVariableA("KAROO_AUTO_EXIT_SECS", buf, sizeof(buf)))
        g_auto_exit_s = (DWORD)atoi(buf);

    if (!g_skip_launcher)
        return;

    log_write("launcher: skipping the launcher dialog (window is still shown), auto_exit_secs=%lu\n", g_auto_exit_s);

    if (g_auto_exit_s > 0) {
        log_write("launcher: auto-exit in %lu s\n", g_auto_exit_s);
        windev::requestCloseAfter(g_auto_exit_s * 1000);
    }
}

bool launcher_skipped(void)
{
    return g_skip_launcher != 0;
}
