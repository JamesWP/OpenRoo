#include "levelreport.h"
#include "menu.h"
#include "logger.h"
#include <stdlib.h>

/* The three polls are consecutive and the first polls of L in the run, so a
 * countdown of three is exact. */
static int  g_left    = -1;  // -1 until read
static bool g_enabled = false;

static void init(void)
{
    if (g_left >= 0) return;
    const char *e = getenv("KAROO_LEVEL_REPORT");
    g_enabled = (e && *e && *e != '0');
    g_left    = g_enabled ? 3 : 0;
    if (g_enabled)
        g_logger.write("levelreport: KAROO_LEVEL_REPORT set — will answer the next "
                  "three VK_L queries as down (Game::LoadSounds trigger)\n");
}

bool levelreport_enabled(void)
{
    init();
    return g_enabled;
}

bool levelreport_async_override(int vkey, short *out)
{
    init();
    if (!g_enabled || g_left <= 0) return false;
    if (vkey != 0x4C )   return false;  // VK_L
    g_left--;
    if (g_left == 0)
        g_logger.write("levelreport: trigger delivered; WriteLevelReport should now "
                  "run over every level\n");
    *out = (short)0x8000;
    return true;
}

void levelreport_tick(void)
{
    init();
    if (!g_enabled || g_left != 0) return;  // not enabled, or not yet fired

    // The report is written before the first frame boundary, so by the time
    // this runs the only job left is to leave.  The menu driver needs several
    // frames to reach Quit; ask every frame.
    static bool said;
    if (!said) {
        said = true;
        g_logger.write("levelreport: report written — quitting via the menu\n");
    }
    menu_request(MENU_NODE_QUIT);
}
