/* FORMAT: SavedGames\<name><N>.sav is one slot record, enciphered byte by byte
 * as file = stored + key (8 bits), opened in text mode like the high-score
 * file (highscores.cpp).
 *
 * PRESERVED:
 *   - The reader fails the whole call at the first missing file; the
 *     writer skips a slot it cannot open and returns 1 regardless.
 *   - The reader's record pointer runs on across slots rather than being
 *     recomputed per slot.
 *   - The reader ignores fread's result and reuses one byte variable, so a
 *     short file repeats its last byte.
 *   - Paths are formatted unbounded into 128-byte buffers. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "saveslots.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdlib.h>

#define PS_LOG_FIRST   6

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < PS_LOG_FIRST) {
        s_logged++;
        log_write("saveslots: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key)
{
    SaveSlots *table = self;
    unsigned char *rec = (unsigned char *)table->slot(0);  // PRESERVED: runs on across slots
    char path[128];
    unsigned char b;  // PRESERVED: never re-initialised
    int slot;

    for (slot = 0; slot < (int)table->count(); slot++) {
        FILE *fp;
        sprintf(path, "%s\\SavedGames\\%s%d.sav", g_gameDir, name, slot);
        fp = fopen(path, "r");
        if (fp == NULL) {
            ps_log("sav load", path, 0);
            return 0;  // PRESERVED: the whole call fails
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
        // Recomputed per slot, unlike the reader.
        unsigned char *rec = (unsigned char *)table->slot((unsigned char)slot);
        FILE *fp;
        sprintf(path, "%s\\SavedGames\\%s%d.sav", g_gameDir, name, slot);
        fp = fopen(path, "w+");
        if (fp == NULL) {
            ps_log("sav save", path, 0);
            continue;  // PRESERVED: skip it and go on
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

/* The table is embedded in the Game, so the deleting destructor never frees in
 * practice. */
extern "C" {

static void *const g_SaveSlotsVtable[1] = { (void *)&SaveSlots_ScalarDtor };

__declspec(dllexport) void *SaveSlots_Vtable(void)
{
    return (void *)g_SaveSlotsVtable;
}

__declspec(dllexport) void __attribute__((thiscall))
SaveSlots_InstallVtable(SaveSlots *self)
{
    *(const void **)self = SaveSlots_Vtable();
}

__declspec(dllexport) void __attribute__((thiscall))
SaveSlots_RestoreVtable(SaveSlots *self)
{
    *(const void **)self = SaveSlots_Vtable();
}

__declspec(dllexport) void *__attribute__((thiscall))
SaveSlots_ScalarDtor(SaveSlots *self, unsigned int flags)
{
    SaveSlots_RestoreVtable(self);
    if (flags & 1)
        free(self);
    return self;
}

}

/* Blanks the first count_ records, re-reading the count each pass.  PRESERVED:
 * the score and unused tail are not touched, so a blank slot keeps a stale
 * score. */
void SaveSlots::initialiseEmpty()
{
    for (int i = 0; i < count_; ++i) {
        SaveSlot &s = slots_[i];
        s.inUse          = 0;
        s.levelIndex     = 0;
        s.livesRemaining = 0;
        strcpy(s.name, "..........");
        s.completionNumerator = 0;
        s.elapsedGameTime     = 0;
    }
}

