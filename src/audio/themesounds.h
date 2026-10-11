#pragma once
#include <stdint.h>
#include "soundasset.h"

/* The theme sound table ("TSM" in its log line).  A .thm `Sound <event>
 * <wave>` line, which the theme loader (render/theme.cpp) hands back to the
 * level entry, fills entries[id] through ThemeSoundTable::addEvent; the id is
 * the ThemeSound value for the event name.  The largest id is 0x47, but the
 * table holds 100 entries, all of which releaseAll clears. */
#define THEME_SOUND_COUNT 100

/* The slots of the theme sound table, named after the theme file's "sound"
 * keywords (themesounds.cpp).  JJ is the player character. */
enum class ThemeSound : unsigned {
    MoveCatcher          = 0,
    MoveJJ               = 1,
    MoveThrower          = 2,
    MoveIceSliding       = 3,
    MoveSliding          = 4,
    MoveParagliding      = 5,
    MoveParaglidingStart = 6,
    MoveJumpPad          = 7,
    Teleporter           = 8,
    Elevator             = 9,
    Platform             = 10,
    Switch               = 11,
    Glue                 = 12,
    Bridge               = 13,
    DestructStart        = 14,
    DestructRegen        = 15,
    Obstacle             = 16,
    Crystal              = 0x1e,
    SplatJJ              = 0x28,
    SplatCatcher         = 0x29,
    SplatThrower         = 0x2a,
    FallJJ               = 0x32,
    FallCatcher          = 0x33,
    FallThrower          = 0x34,
    BombTick             = 0x3c,
    ExplosionBomb        = 0x46,
    ExplosionCatcher     = 0x47,
};
class ThemeSoundTable {
public:
    /* Adds (or replaces) the wave for a theme sound id. */
    int add(const char *dir, unsigned int id, const char *waveName);

    /* The same, for a theme file's `sound` line: the event is the slot's
     * keyword (movejj, bombtick..., any case).  False, adding nothing, for an
     * event no slot has. */
    bool addEvent(const char *dir, const char *eventName, const char *waveName);

    /* An empty table: every entry cleared (see releaseAll). */
    ThemeSoundTable();
    virtual ~ThemeSoundTable();
    ThemeSoundTable(const ThemeSoundTable &) = delete;
    ThemeSoundTable &operator=(const ThemeSoundTable &) = delete;

    int releaseAll();

    /* The entry for a theme sound slot. */
    const SoundAssetName *entry(ThemeSound s) const { return &entries_[static_cast<unsigned>(s)]; }

private:
    uint32_t          unknown4_{};     /* +4  never written */
    uint16_t           unknown8_{};     /* +8  zeroed by the ctor, never read */
    SoundAssetName entries_[THEME_SOUND_COUNT];
};
