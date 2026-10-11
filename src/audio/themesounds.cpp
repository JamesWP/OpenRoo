#include "themesounds.h"
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "sysdev.h"

/* "NONE" (case-exact, on the raw wave name) disables the entry without
 * logging.  PRESERVED: the path is formatted unbounded into 256 bytes. */
int ThemeSoundTable::add(const char *dir, unsigned int id, const char *waveName)
{
    char path[256];
    snprintf(path, sizeof(path), "%s\134%s", dir, waveName);

    SoundAssetName &e = entries_[id & 0xffff];
    if (strcmp(waveName, "NONE") == 0) {
        e.enabled = 0;
        return 0;
    }
    g_logger.logMessage(1, "TSM: add called (Index=%d/fn=%s)", id & 0xffff, path);
    strcpy(e.name, path);
    e.enabled    = 1;
    return 0;
}

/* The event keywords, in test order. */
static const struct { const char *name; ThemeSound id; } kSoundEvents[] = {
    { "movejj", ThemeSound::MoveJJ },                 { "movecatcher", ThemeSound::MoveCatcher },
    { "movethrower", ThemeSound::MoveThrower },       { "moveicesliding", ThemeSound::MoveIceSliding },
    { "movesliding", ThemeSound::MoveSliding },       { "moveparagliding", ThemeSound::MoveParagliding },
    { "moveparaglidingstart", ThemeSound::MoveParaglidingStart }, { "movejumppad", ThemeSound::MoveJumpPad },
    { "teleporter", ThemeSound::Teleporter },         { "elevator", ThemeSound::Elevator },
    { "platform", ThemeSound::Platform },             { "switch", ThemeSound::Switch },
    { "glue", ThemeSound::Glue },                     { "bridge", ThemeSound::Bridge },
    { "destructstart", ThemeSound::DestructStart },   { "destructregen", ThemeSound::DestructRegen },
    { "obstacle", ThemeSound::Obstacle },             { "crystal", ThemeSound::Crystal },
    { "splatjj", ThemeSound::SplatJJ },               { "splatcatcher", ThemeSound::SplatCatcher },
    { "splatthrower", ThemeSound::SplatThrower },     { "falljj", ThemeSound::FallJJ },
    { "fallcatcher", ThemeSound::FallCatcher },       { "fallthrower", ThemeSound::FallThrower },
    { "bombtick", ThemeSound::BombTick },             { "explosionbomb", ThemeSound::ExplosionBomb },
    { "explosioncatcher", ThemeSound::ExplosionCatcher },
};

bool ThemeSoundTable::addEvent(const char *dir, const char *eventName, const char *waveName)
{
    for (const auto &ev : kSoundEvents) {
        if (sysdev::compareNoCase(eventName, ev.name) == 0) {
            add(dir, static_cast<unsigned>(ev.id), waveName);
            return true;
        }
    }
    return false;
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
