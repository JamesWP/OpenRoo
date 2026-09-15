/* SavedGames\<name><N>.sav -- the save-slot table's files (saveslots.h).
 * Split from the old playerstate.cpp; ASSET_PLAN.md Phase 2's notes on the
 * format (the cipher, TEXT mode, the reader/writer asymmetries) are in
 * highscores.cpp's header.
 *
 *   0x43b4a0  LoadSaveFile           (this, name, key)   ret 8   1 site
 *   0x43b3d0  WriteAllSaveSlotFiles  (this, name, key)   ret 8   2 sites
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "saveslots.h"

/* Game data the path formats consume.  A DATA read, not a call. */
#define GAME_DIR       ((const char *)0x004e01c4)

#define PS_LOG_FIRST   6

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < PS_LOG_FIRST) {
        s_logged++;
        log_write("saveslots: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

/* ─── SavedGames\<name><N>.sav ───────────────────────────────────────────── */

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key)
{
    SaveSlots *table = self;
    unsigned char *rec = (unsigned char *)table->slot(0);  /* runs continuously - defect 4 */
    char path[128];
    unsigned char b;                              /* not re-initialised - defect 2 */
    int slot;

    for (slot = 0; slot < (int)table->count(); slot++) {
        FILE *fp;
        sprintf(path, "%s\\SavedGames\\%s%d.sav", GAME_DIR, name, slot);
        fp = fopen(path, "r");
        if (fp == NULL) {
            ps_log("sav load", path, 0);
            return 0;                             /* defect 3: whole call fails */
        }
        for (int i = 0; i < (int)sizeof(SaveSlot); i++) {
            fread(&b, 1, 1, fp);
            *rec++ = (unsigned char)(b - (unsigned char)key);
        }
        fclose(fp);
        ps_log("sav load", path, 1);
    }
    return 1;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_WriteAllSlotFiles(SaveSlots *self, const char *name, char key)
{
    SaveSlots *table = self;
    char path[128];
    int slot;

    for (slot = 0; slot < (int)table->count(); slot++) {
        /* Recomputed per slot, unlike the reader -- defect 4. */
        unsigned char *rec = (unsigned char *)table->slot((unsigned char)slot);
        FILE *fp;
        sprintf(path, "%s\\SavedGames\\%s%d.sav", GAME_DIR, name, slot);
        fp = fopen(path, "w+");
        if (fp == NULL) {
            ps_log("sav save", path, 0);
            continue;                             /* defect 3: skip, keep going */
        }
        for (int i = 0; i < (int)sizeof(SaveSlot); i++) {
            unsigned char out = (unsigned char)(rec[i] + (unsigned char)key);
            fwrite(&out, 1, 1, fp);
        }
        fclose(fp);
        ps_log("sav save", path, 1);
    }
    return 1;
}

