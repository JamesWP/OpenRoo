#include <stdint.h>
#include "sysdev.h"
#include <stdlib.h>
#include "logger.h"
#include "windev.h"
#include "launcher.h"

static int   g_skip_launcher    = 0;
static uint32_t g_auto_exit_s = 0;

void launcher_end_run(const char *why)
{
    static bool done = false;
    if (done) return;
    done = true;

    g_logger.write("launcher: ending run (%s)\n", why);
    windev::requestClose();
}

void launcher_init(void)
{
    char buf[32];
    if (sysdev::getEnv("KAROO_SKIP_LAUNCHER", buf, sizeof(buf)))
        g_skip_launcher = atoi(buf);
    if (sysdev::getEnv("KAROO_AUTO_EXIT_SECS", buf, sizeof(buf)))
        g_auto_exit_s = (uint32_t)atoi(buf);

    if (!g_skip_launcher)
        return;

    g_logger.write("launcher: skipping the launcher dialog (window is still shown), auto_exit_secs=%lu\n", g_auto_exit_s);

    if (g_auto_exit_s > 0) {
        g_logger.write("launcher: auto-exit in %lu s\n", g_auto_exit_s);
        windev::requestCloseAfter(g_auto_exit_s * 1000);
    }
}

bool launcher_skipped(void)
{
    return g_skip_launcher != 0;
}
