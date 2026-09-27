/* FORMAT: Highscores\<name>.hsc is the first count() records as raw bytes,
 * enciphered byte by byte: file = stored + key, truncated to 8 bits (the
 * caller passes the key).  It is opened in text mode ("r", "w+"), so a 0x0a
 * byte is written as CR LF and read back as one byte; binary mode would write
 * files the game cannot read.
 *
 * PRESERVED:
 *   - The reader ignores fread's result and reads each byte into the same
 *     variable, so a short file repeats its last byte to the end.
 *   - The path is formatted unbounded into a 128-byte buffer. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "highscores.h"
#include <stdlib.h>
#include "gamestr.h"
#include "gameglobals.h"

#include "bytes.h"

#define HSC_ENTRY_SIZE ((unsigned)HIGH_SCORE_RECORD_BYTES)

static void hsc_encode(const HighScoreRecord *r, unsigned char *p)
{
    put_bytes(p, r->name, sizeof(r->name));
    put_u32(p, r->score);
    put_u8(p, r->level);
}

static void hsc_decode(HighScoreRecord *r, const unsigned char *p)
{
    get_bytes(p, r->name, sizeof(r->name));
    r->score = get_u32(p);
    r->level = get_u8(p);
}

#define PS_LOG_FIRST   6

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < PS_LOG_FIRST) {
        s_logged++;
        log_write("highscores: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

int HighScore_ReadFile(HighScoreTable *self, const char *name, char key)
{
    return self->readFile(name, key);
}

int HighScoreTable::readFile(const char *name, char key)
{
    unsigned char rec[HSC_ENTRY_SIZE];
    char path[128];
    unsigned char b = 0;  // PRESERVED: never re-initialised between reads

    sprintf(path, "%s\\Highscores\\%s.hsc", g_gameDir, name);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        ps_log("hsc load", path, 0);
        return 0;
    }
    for (unsigned r = 0; r < count_; r++) {
        for (unsigned i = 0; i < HSC_ENTRY_SIZE; i++) {
            fread(&b, 1, 1, fp);
            rec[i] = (unsigned char)(b - (unsigned char)key);
        }
        hsc_decode(&records_[r], rec);
    }
    fclose(fp);
    ps_log("hsc load", path, 1);
    return 1;
}

int HighScore_WriteFile(HighScoreTable *self, const char *name, char key)
{
    return self->writeFile(name, key);
}

int HighScoreTable::writeFile(const char *name, char key)
{
    unsigned char rec[HSC_ENTRY_SIZE];
    char path[128];

    sprintf(path, "%s\\Highscores\\%s.hsc", g_gameDir, name);
    FILE *fp = fopen(path, "w+");
    if (fp == NULL) {
        ps_log("hsc save", path, 0);
        return 0;
    }
    for (unsigned r = 0; r < count_; r++) {
        hsc_encode(&records_[r], rec);
        for (unsigned i = 0; i < HSC_ENTRY_SIZE; i++) {
            unsigned char out = (unsigned char)(rec[i] + (unsigned char)key);
            fwrite(&out, 1, 1, fp);
        }
    }
    fclose(fp);
    ps_log("hsc save", path, 1);
    return 1;
}

/* PRESERVED: scores compare unsigned.  Shifting down starts one record past
 * the last, so a full table writes past its end.  Only the name is cleared
 * before the new score and level are stored.  The return
 * is 11 * rank + 11 with the low byte replaced by the rank.
 *
 * KAROO_SIM_FX=hsnoplace is a negative control: no score places, so the game
 * never asks for a name. */
static int s_hs_fx = -1;

unsigned int
Sim_InsertScoreIntoHighScoreTable(HighScoreTable *self, unsigned int score,
                                  unsigned char levelId)
{
    return self->insert(score, levelId);
}

unsigned int HighScoreTable::insert(unsigned int score, unsigned char levelId)
{
    unsigned int count = count_;
    unsigned int rank;

    if (s_hs_fx < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
        s_hs_fx = (n > 0 && n < sizeof(buf) && strcmp(buf, "hsnoplace") == 0);
        if (s_hs_fx)
            log_write("highscores: KAROO_SIM_FX=hsnoplace -- no score places\n");
    }

    for (rank = 0; (int)rank < (int)count; ++rank)
        if (score > records_[rank].score)
            break;
    if ((int)rank >= (int)count || s_hs_fx)
        return (count & 0xffffff00u) | 0xffu;

    // Records count + 1 down to rank + 1 each take the one before, through raw
    // pointers so writing past the array is well defined.
    if ((int)count >= (int)rank) {
        unsigned char *dst = (unsigned char *)records_ + (count + 1) * sizeof(HighScoreRecord);
        for (unsigned int n = count - rank + 1; n != 0; --n) {
            memcpy(dst, dst - sizeof(HighScoreRecord), sizeof(HighScoreRecord));
            dst -= sizeof(HighScoreRecord);
        }
    }
    memset(records_[rank].name, 0, sizeof(records_[rank].name));
    records_[rank].score = score;
    records_[rank].level = levelId;
    lastRank_ = (unsigned char)rank;
    return ((rank * 11 + 11) & 0xffffff00u) | (rank & 0xffu);
}

static void *const g_HighScoreVtable[1] = { (void *)&HighScoreTable_ScalarDestructor };

void HighScoreTable::construct() { vtable_ = g_HighScoreVtable; }
void HighScoreTable::destruct()  { vtable_ = g_HighScoreVtable; }

HighScoreTable *
HighScoreTable_ScalarDestructor(HighScoreTable *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* The ten defaults are written whatever count() is; the name only into the
 * first count() rows. */
void HighScoreTable::fillDefaults()
{
    static const struct { unsigned int score; unsigned char level; } def[10] = {
        { 10770, 80 }, { 9785, 71 }, { 8750, 64 }, { 7840, 55 }, { 6120, 42 },
        {  5235, 33 }, { 3685, 20 }, { 2785, 15 }, { 1815, 11 }, {  970,  6 },
    };
    for (int i = 0; i < (int)count_; i++)
        strcpy(records_[i].name, GS_HIGHSCORE_DEFAULT_NAME);
    for (int i = 0; i < 10; i++) {
        records_[i].score = def[i].score;
        records_[i].level = def[i].level;
    }
}
