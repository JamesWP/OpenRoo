/* HighScoreTable -- the high-score table embedded in Game at +0x13cdbb
 * (COHESION_PLAN.md Band 3 follow-on; the sixth sub-object).
 *
 * 0x3706 bytes: 256 records of 0x37 from +0x5 end at the count byte
 * +0x3705, and the object ends exactly at the score tally (+0x1404c1).
 * KAROO_LAYOUT_SIZE asserts it.
 *
 *   InstallHighScoreTableVtable 0x41edf0  ctor: stores vtable 0x45d420, nothing else
 *   FillDefaultHighScores       0x41ee30  "Bernie Boulder" and ten fixed
 *                                         score/level pairs, for count records
 *   ReadHighScoreFile           0x41ef20  ours: readFile   (highscores.cpp)
 *   WriteHighScoreFile          0x41efe0  ours: writeFile  (highscores.cpp)
 *   InsertScoreIntoHighScoreTable 0x41f0a0  ours: insert (highscores.cpp)
 *
 * Game::Load sets the count to 10.  The .hsc file is the records as raw
 * bytes, count*0x37 of them, enciphered (highscores.cpp).
 */
#pragma once

#include "layout.h"

/* One row: the name the player typed, the score and the level reached. */
struct __attribute__((packed)) HighScoreRecord {
    char          name[0x32];
    unsigned int  score;
    unsigned char level;
};

class __attribute__((packed)) HighScoreTable {
public:
    static const int ORIGIN = 0;

    enum { RECORD_MAX = 256 };

    int           readFile(const char *name, char key);
    int           writeFile(const char *name, char key);
    /* Place `score` (returns the rank in AL, 0xff when it does not place;
     * see highscores.cpp for the rest of EAX). */
    unsigned int  insert(unsigned int score, unsigned char levelId);

    unsigned char count() const                      { return count_; }
    void          setCount(unsigned char n)          { count_ = n; }
    /* The rank insert() last placed a score at. */
    unsigned char lastRank() const                   { return lastRank_; }
    HighScoreRecord       *record(unsigned int i)       { return &records_[i]; }
    const HighScoreRecord *record(unsigned int i) const { return &records_[i]; }


    /* Game-embedded lifecycle, called only by Game_Construct / Game_Destruct
     * (gamelife.cpp).  The vtable installed is ours (one slot, the scalar
     * dtor below); the game's is left as a tripwire. */
    void construct();   /* 0x41edf0 */
    void destruct();    /* 0x41ee20 */
    /* 0x41ee30 -- "Bernie Boulder" in the first count() names, then ten
     * fixed score/level pairs.  Its four stack arguments are ignored. */
    void fillDefaults();

private:
    HighScoreTable() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(HighScoreTable);

    const void      *vtable_;                        /* +0x00  0x45d420 */
    unsigned char    lastRank_;                      /* +0x04 */
    HighScoreRecord  records_[RECORD_MAX];           /* +0x05 */
    unsigned char    count_;                         /* +0x3705 */
};

KAROO_LAYOUT_CHECKS(HighScoreTable)
{
    KAROO_LAYOUT_AT(lastRank_, 0x04);
    KAROO_LAYOUT_AT(records_,  0x05);
    KAROO_LAYOUT_AT(count_,    0x3705);
    KAROO_LAYOUT_SIZE(0x3706);
}

/* The exports patch.py binds; shims onto the methods. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
HighScore_ReadFile(HighScoreTable *self, const char *name, char key);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
HighScore_WriteFile(HighScoreTable *self, const char *name, char key);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_InsertScoreIntoHighScoreTable(HighScoreTable *self, unsigned int score,
                                  unsigned char levelId);

/* 0x41ee00, slot 0 of our HighScoreTable table. */
extern "C" __declspec(dllexport) HighScoreTable *__attribute__((thiscall))
HighScoreTable_ScalarDestructor(HighScoreTable *self, unsigned char flags);
