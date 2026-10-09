#include "gamedir.h"
#include <stdio.h>

static char g_dir[64];

const char *gameDir() { return g_dir; }

void setGameDir(const char *dir)
{
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
}
