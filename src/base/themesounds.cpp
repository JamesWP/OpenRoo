#include "themesounds.h"
#include <stdio.h>
#include <string.h>
#include "gamestr.h"
#include "logger.h"

/* "NONE" (case-exact, on the raw wave name) disables the entry without
 * logging.  PRESERVED: the path is formatted unbounded into 256 bytes. */
int ThemeSoundTable::add(const char *dir, unsigned int id, const char *waveName,
               uint32_t arg3, uint32_t arg4)
{
    char path[256];
    snprintf(path, sizeof(path), GS_THEME_SOUND_PATH, dir, waveName);

    SoundAssetName &e = entries_[id & 0xffff];
    if (strcmp(waveName, GS_THEME_SOUND_NONE) == 0) {
        e.enabled = 0;
        return 0;
    }
    g_logger.logMessage(1, "TSM: add called (Index=%d/fn=%s)", id & 0xffff, path);
    strcpy(e.name, path);
    e.unknown104 = arg4;
    e.unknown108 = arg3;
    e.enabled    = 1;
    return 0;
}

/* The theme sound table's lifecycle.  ReleaseAll clears the name and the
 * enabled flag of all 100 entries between two log lines, leaving the other
 * fields, and returns 0. */
int ThemeSoundTable::releaseAll()
{
    g_logger.logMessage(1, "TSM: trying to release all sounds");
    for (int i = 0; i < THEME_SOUND_COUNT; i++) {
        entries_[i].enabled = 0;
        entries_[i].name[0] = 0;
    }
    g_logger.logMessage(1, "TSM: all sounds released");
    return 0;
}

ThemeSoundTable::ThemeSoundTable()
{
    unknown8_  = 0;
    releaseAll();
}

ThemeSoundTable::~ThemeSoundTable()
{
}
