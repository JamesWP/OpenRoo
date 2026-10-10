#include "gametime.h"
#include <stdlib.h>
#include <time.h>
#include "sysdev.h"
#include "logger.h"

static int  g_seed      = 0;
static bool g_seed_set  = false;
static bool g_seed_read = false;

int hooks_GameTime(int *out)
{
    if (!g_seed_read) {
        char buf[32];
        g_seed_read = true;
        if (sysdev::getEnv("KAROO_SEED", buf, sizeof(buf)) && buf[0]) {
            g_seed     = atoi(buf);
            g_seed_set = true;
        }
        g_logger.write("clock: seed = %s (%d)\n",
                  g_seed_set ? "FIXED" : "wall clock", g_seed);
    }

    if (!g_seed_set)
        return (int)time((time_t *)out);

    if (out) *out = g_seed;
    return g_seed;
}
