/* The high-score table's file and insert (highscores.h).  Split from the
 * old playerstate.cpp with config.cpp (Karoo.cfg) and saveslots.cpp (*.sav);
 * ASSET_PLAN.md Phase 2's notes on all three formats stay here.
 *
 *   0x41d3e0  LoadConfigValues        (this, path)            ret 4   1 site
 *   0x41d490  SaveConfig              (this, path)            ret 4   2 sites
 *   0x43b4a0  LoadSaveFile            (this, name, key)       ret 8   1 site
 *   0x43b3d0  WriteAllSaveSlotFiles   (this, name, key)       ret 8   2 sites
 *   0x41ef20  ReadHighScoreFile       (this, name, key)       ret 8   1 site
 *   0x41efe0  WriteHighScoreFile      (this, name, key)       ret 8   4 sites
 *
 * All eleven references are E8; no E9, no PUSH, no vtable slot.  Every
 * original is UD2-stubbed.
 *
 * This is the first phase that owns files the game WRITES, so the bar is
 * higher than "it loads": a byte the writer emits differently is a save
 * fixture that no longer restores and a replay suite that fails at its
 * hash check.  Readers were written and shipped before writers within each
 * format, per ASSET_PLAN.md's hazard list.
 *
 * ─── No calls into the game binary ────────────────────────────────────────
 *
 * Our CRT opens, reads, writes and closes.  The only game addresses touched
 * are DATA: the game-directory buffer at 0x4e01c4 that the path formats
 * consume, and the caller's object.  Reading the game's memory is not a
 * callback.
 *
 * ─── The obfuscation ──────────────────────────────────────────────────────
 *
 * .sav and .hsc are enciphered with a per-byte additive cipher whose key the
 * CALLER passes as a char argument: stored = file_byte - key on read, and
 * file_byte = stored + key on write, both truncated to 8 bits.  tools/
 * karoosave.py independently documents key 0x37 for .sav (and 5 for JJ.GAM),
 * derived from the call sites; the disassembly of these six functions agrees,
 * which is a second, independent confirmation of that key.
 *
 * .cfg is NOT enciphered.  It is a 0x144e-byte blob plus a 10-byte tag, and
 * the tag must equal "End" (0x4661dc) for the config to be accepted.
 *
 * ─── TEXT MODE, which is load-bearing ─────────────────────────────────────
 *
 * Every one of these six opens in TEXT mode, never binary:
 *
 *     .cfg   read "r"    write "w"
 *     .sav   read "r"    write "w+"
 *     .hsc   read "r"    write "w+"
 *
 * On Windows that means a 0x0A byte written becomes 0x0D 0x0A on disk, and is
 * collapsed back on read.  It is not cosmetic: Karoo.cfg is 5209 bytes on disk
 * while the game writes 5198 + 10 = 5208 logical bytes, and the difference is
 * exactly one such translated newline.  Opening any of these "rb"/"wb" would
 * produce files the game cannot read back and would break every save fixture,
 * so the modes are reproduced exactly.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. SaveConfig WRITES 6 BYTES OF UNINITIALISED STACK.  It copies "End" (4
 *    bytes with its NUL) into a 10-byte buffer and writes all 10.  The last
 *    six are whatever was on the stack.  Reproduced: the buffer is left
 *    uninitialised and only the tag is written into it, exactly as the
 *    original does, per CLAUDE.md's "leave uninitialised fields
 *    uninitialised".  LoadConfigValues only ever compares the first four via
 *    strcmp, so the garbage is never read back.
 *
 * 2. THE READERS DO NOT CHECK fread's RESULT.  Each byte is a separate
 *    fread(&b, 1, 1, fp) into the SAME one-byte variable, which is not
 *    re-initialised.  On a short file every remaining byte therefore repeats
 *    the last one successfully read (and the very first repeats whatever was
 *    in that stack slot).  Reproduced by ignoring fread's return, rather than
 *    "fixed" into a zero fill or an error.
 *
 * 3. LoadSaveFile FAILS THE WHOLE CALL on the first missing slot file and
 *    returns 0, whereas WriteAllSaveSlotFiles SKIPS a slot it cannot open,
 *    advances to the next, and returns 1 regardless.  The two are not
 *    symmetric; both behaviours are kept.
 *
 * 4. LoadSaveFile's record pointer runs CONTINUOUSLY across slots -- it is not
 *    recomputed per slot -- so slot N's 42 bytes land at this+0x31+N*0x2a only
 *    because the previous slot consumed exactly 42.  The writer, by contrast,
 *    recomputes slotPtr = this+0x31+N*0x2a each iteration.  Same addresses,
 *    different mechanism, and only the writer's survives a failed slot.
 *
 * 5. THE HIGH-SCORE LOOPS RE-READ THEIR COUNT EVERY ITERATION from
 *    this+0x3705 and mask the index to 16 bits (AND with 0xffff).  The count
 *    is 55 * n bytes.  Both are reproduced; with n a byte the mask can never
 *    engage (255*55 = 14025), so it is faithfulness, not a live behaviour.
 *
 * 6. Both high-score functions and both save functions build their path with
 *    the game's sprintf into a ~128-byte stack buffer with no bounds check.
 *    Kept at 128 and unchecked.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "highscores.h"
#include <stdlib.h>
#include "gamestr.h"
#include "gameglobals.h"

/* Game data the path formats consume.  DATA reads, not calls. */


/* High scores: the records as raw bytes, count*0x37 of them (highscores.h). */
#define HSC_ENTRY_SIZE ((unsigned)sizeof(HighScoreRecord))

#define PS_LOG_FIRST   6

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < PS_LOG_FIRST) {
        s_logged++;
        log_write("highscores: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

/* ─── Highscores\<name>.hsc ──────────────────────────────────────────────── */

extern "C" __declspec(dllexport) int __attribute__((thiscall))
HighScore_ReadFile(HighScoreTable *self, const char *name, char key)
{
    return self->readFile(name, key);
}

int HighScoreTable::readFile(const char *name, char key)
{
    unsigned char *data = (unsigned char *)records_;
    char path[128];
    unsigned char b;                              /* defect 2 */
    unsigned i = 0;

    sprintf(path, "%s\\Highscores\\%s.hsc", g_gameDir, name);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        ps_log("hsc load", path, 0);
        return 0;
    }
    /* Count re-read every iteration, index masked to 16 bits -- defect 5. */
    while ((i & 0xffff) < (unsigned)(count_ * HSC_ENTRY_SIZE)) {
        fread(&b, 1, 1, fp);
        data[i & 0xffff] = (unsigned char)(b - (unsigned char)key);
        i++;
    }
    fclose(fp);
    ps_log("hsc load", path, 1);
    return 1;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
HighScore_WriteFile(HighScoreTable *self, const char *name, char key)
{
    return self->writeFile(name, key);
}

int HighScoreTable::writeFile(const char *name, char key)
{
    const unsigned char *data = (const unsigned char *)records_;
    char path[128];
    unsigned i = 0;

    sprintf(path, "%s\\Highscores\\%s.hsc", g_gameDir, name);
    FILE *fp = fopen(path, "w+");
    if (fp == NULL) {
        ps_log("hsc save", path, 0);
        return 0;
    }
    while ((i & 0xffff) < (unsigned)(count_ * HSC_ENTRY_SIZE)) {
        unsigned char out = (unsigned char)(data[i & 0xffff] + (unsigned char)key);
        fwrite(&out, 1, 1, fp);
        i++;
    }
    fclose(fp);
    ps_log("hsc save", path, 1);
    return 1;
}

/* ─── InsertScoreIntoHighScoreTable 0x0041f0a0 ────────────────────────────
 *
 * GAMETICK_PLAN.md Band B reopened.  __thiscall(HighScoreTable*, uint score,
 * BYTE levelId), RET 8.  A LEAF; one E8 site, 0x00416147 in GameTick, which
 * tests only AL (CMP AL,0xff / JNC).  Transcribed from the LISTING:
 *
 *  - scan ranks 0..count-1 for the first record whose score is UNSIGNED-
 *    below the new one (JA); none -> return count with AL forced to 0xff;
 *  - if count >= rank, shift records count-1..rank up one slot, starting at
 *    index COUNT -- so a full table writes one record past its end (the slot
 *    is inside the allocation; preserved);
 *  - zero the freed record's 50-byte name (12 dwords + a word -- NOT the
 *    whole 0x32+4, the score dword is left for the store below);
 *  - store score and levelId, rank into this+4, and return EAX as the
 *    listing leaves it: (11*rank+11) with AL = rank.
 *
 * The count at +0x3705 is not incremented here; the caller does that.
 *
 * Control: KAROO_SIM_FX=hsnoplace -- never places (returns 0xff after the
 * scan), so the caller never enters the name-entry state 6.
 */
static int s_hs_fx = -1;

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
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

    /* Records count+1 down to rank+1 each take the one before; a full
     * table (count 256) writes record 256, past the array -- into the
     * tally, as the original does.  Raw pointers so that is well-defined. */
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

/* ─── Lifecycle and defaults (Game TU) ────────────────────────────────────── */
static void *const g_HighScoreVtable[1] = { (void *)&HighScoreTable_ScalarDestructor };

void HighScoreTable::construct() { vtable_ = g_HighScoreVtable; }
void HighScoreTable::destruct()  { vtable_ = g_HighScoreVtable; }

extern "C" __declspec(dllexport) HighScoreTable *__attribute__((thiscall))
HighScoreTable_ScalarDestructor(HighScoreTable *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* The ten defaults are written whatever count() is; the name only into the
 * first count() records. */
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
