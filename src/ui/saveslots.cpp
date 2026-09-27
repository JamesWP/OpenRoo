/* FORMAT: SavedGames\<name><N>.sav is one slot record, enciphered byte by byte
 * as file = stored + key (8 bits), opened in text mode like the high-score
 * file (highscores.cpp).
 *
 * PRESERVED:
 *   - The reader fails the whole call at the first missing file; the
 *     writer skips a slot it cannot open and returns 1 regardless.
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
#include "bytes.h"
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

void SaveSlot_Encode(const SaveSlot *s, unsigned char out[SAVE_SLOT_BYTES])
{
    unsigned char *p = out;
    put_bytes(p, s->name, sizeof(s->name));
    put_u8(p, s->levelIndex);
    put_u8(p, s->livesRemaining);
    put_u32(p, s->totalScore);
    put_u32(p, s->completionNumerator);
    put_u32(p, s->elapsedGameTime);
    put_u32(p, s->inUse);
    put_u32(p, s->unusedTail);
}

void SaveSlot_Decode(SaveSlot *s, const unsigned char in[SAVE_SLOT_BYTES])
{
    const unsigned char *p = in;
    get_bytes(p, s->name, sizeof(s->name));
    s->levelIndex          = get_u8(p);
    s->livesRemaining      = get_u8(p);
    s->totalScore          = get_u32(p);
    s->completionNumerator = get_u32(p);
    s->elapsedGameTime     = get_u32(p);
    s->inUse               = get_u32(p);
    s->unusedTail          = get_u32(p);
}

int Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key)
{
    SaveSlots *table = self;
    unsigned char rec[SAVE_SLOT_BYTES];
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
        for (int i = 0; i < SAVE_SLOT_BYTES; i++) {
            fread(&b, 1, 1, fp);
            rec[i] = (unsigned char)(b - (unsigned char)key);
        }
        SaveSlot_Decode(table->slot((unsigned char)slot), rec);
        fclose(fp);
        ps_log("sav load", path, 1);
    }
    return 1;
}

int Save_WriteAllSlotFiles(SaveSlots *self, const char *name, char key)
{
    SaveSlots *table = self;
    char path[128];
    int slot;

    for (slot = 0; slot < (int)table->count(); slot++) {
        unsigned char rec[SAVE_SLOT_BYTES];
        SaveSlot_Encode(table->slot((unsigned char)slot), rec);
        FILE *fp;
        sprintf(path, "%s\\SavedGames\\%s%d.sav", g_gameDir, name, slot);
        fp = fopen(path, "w+");
        if (fp == NULL) {
            ps_log("sav save", path, 0);
            continue;  // PRESERVED: skip it and go on
        }
        for (int i = 0; i < SAVE_SLOT_BYTES; i++) {
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

static void *const g_SaveSlotsVtable[1] = { (void *)&SaveSlots_ScalarDtor };

void *SaveSlots_Vtable(void)
{
    return (void *)g_SaveSlotsVtable;
}

void SaveSlots_InstallVtable(SaveSlots *self)
{
    *(const void **)self = SaveSlots_Vtable();
}

void SaveSlots_RestoreVtable(SaveSlots *self)
{
    *(const void **)self = SaveSlots_Vtable();
}

void *SaveSlots_ScalarDtor(SaveSlots *self, unsigned int flags)
{
    SaveSlots_RestoreVtable(self);
    if (flags & 1)
        free(self);
    return self;
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

void SaveSlots_InitialiseEmpty(SaveSlots *self)
{
    self->initialiseEmpty();
}
