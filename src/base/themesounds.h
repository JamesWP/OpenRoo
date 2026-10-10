#pragma once
#include <stdint.h>
#include "soundasset.h"

/* The theme sound table ("TSM" in its log line),.  A .thm
 * `Sound <event> <wave>` line fills entries[id] through ThemeSoundTable::add
 * (theme.cpp); the id is RegisterThemeSound's event number, so e.g. entry 0
 * is movecatcher and entry 70 explosionbomb.  The event table's largest id is
 * 0x47, but the table holds 100 entries: ReleaseAll clears exactly
 * 100, where switchMax_ begins.  Lifecycle in
 * theme.cpp; the vptr is at +0. */
#define THEME_SOUND_COUNT 100
class ThemeSoundTable {
public:
    /* Adds (or replaces) the wave for a theme sound id. */
    int add(const char *dir, unsigned int id, const char *waveName, uint32_t arg3, uint32_t arg4);

    /* An empty table: every entry cleared (see releaseAll). */
    ThemeSoundTable();
    virtual ~ThemeSoundTable();
    ThemeSoundTable(const ThemeSoundTable &) = delete;
    ThemeSoundTable &operator=(const ThemeSoundTable &) = delete;

    int releaseAll();

    /* The entry for theme event id. */
    const SoundAssetName *entry(int id) const { return &entries_[id]; }

private:
    uint32_t          unknown4_{};     /* +4  never written */
    uint16_t           unknown8_{};     /* +8  zeroed by the ctor, never read */
    SoundAssetName entries_[THEME_SOUND_COUNT];
};
