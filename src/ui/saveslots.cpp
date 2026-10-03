/* FORMAT: SavedGames\<name><N>.sav is one slot record, enciphered byte by byte
 * as file = stored + key (8 bits), opened in text mode like the high-score
 * file (highscores.cpp).
 *
 * PRESERVED:
 *   - The reader fails the whole call at the first missing file; the
 *     writer skips a slot it cannot open and returns 1 regardless.
 *   - The reader ignores read's result and reuses one byte variable, so a
 *     short file repeats its last byte.
 *   - Paths are formatted unbounded into 128-byte buffers. */
#include <fstream>
#include <stdio.h>
#include <string.h>
#include "logger.h"
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
        g_logger.write("saveslots: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}


void SaveSlot::encode(unsigned char out[SAVE_SLOT_BYTES]) const 
{
    unsigned char *p = out;
    put_bytes(p, name, sizeof(name));
    put_u8(p, levelIndex);
    put_u8(p, livesRemaining);
    put_u32(p, totalScore);
    put_u32(p, completionNumerator);
    put_u32(p, elapsedGameTime);
    put_u32(p, inUse);
    put_u32(p, unusedTail);
}

void SaveSlot::decode(const unsigned char in[SAVE_SLOT_BYTES])
{
    const unsigned char *p = in;
    get_bytes(p, name, sizeof(name));
    levelIndex          = get_u8(p);
    livesRemaining      = get_u8(p);
    totalScore          = get_u32(p);
    completionNumerator = get_u32(p);
    elapsedGameTime     = get_u32(p);
    inUse               = get_u32(p);
    unusedTail          = get_u32(p);
}

int SaveSlots::loadAllSlotFiles(const char *name, char key)
{
    unsigned char rec[SAVE_SLOT_BYTES];
    char path[128];
    unsigned char b;  // PRESERVED: never re-initialised
    int slot;

    for (slot = 0; slot < (int)count(); slot++) {
        sprintf(path, "%s\\SavedGames\\%s%d.sav", g_gameDir, name, slot);
        std::ifstream in(path);  // text mode, as the original wrote them
        if (!in) {
            ps_log("sav load", path, 0);
            return 0;  // PRESERVED: the whole call fails
        }
        for (int i = 0; i < SAVE_SLOT_BYTES; i++) {
            in.read(reinterpret_cast<char *>(&b), 1);
            rec[i] = (unsigned char)(b - (unsigned char)key);
        }
        this->slot((unsigned char)slot)->decode(rec);
        ps_log("sav load", path, 1);
    }
    return 1;
}

int SaveSlots::writeAllSlotFiles(const char *name, char key)
{
    char path[128];
    int slot;

    for (slot = 0; slot < (int)count(); slot++) {
        unsigned char rec[SAVE_SLOT_BYTES];
        this->slot((unsigned char)slot)->encode(rec);
        sprintf(path, "%s\\SavedGames\\%s%d.sav", g_gameDir, name, slot);
        std::ofstream outFile(path);  // text mode, as the original wrote them
        if (!outFile) {
            ps_log("sav save", path, 0);
            continue;  // PRESERVED: skip it and go on
        }
        for (int i = 0; i < SAVE_SLOT_BYTES; i++) {
            unsigned char out = (unsigned char)(rec[i] + (unsigned char)key);
            outFile.write(reinterpret_cast<const char *>(&out), 1);
        }
        outFile.close();
        ps_log("sav save", path, 1);
    }
    return 1;
}

/* Construction and destruction leave the slots alone. */
SaveSlots::SaveSlots()
{
}

SaveSlots::~SaveSlots()
{
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

