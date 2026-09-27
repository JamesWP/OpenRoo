/* HighScoreTable: the high-score table, a sub-object of Game.  The game keeps
 * count() rows (10) of name, score and level, loaded from and saved to
 * Highscores\<name>.hsc (highscores.cpp). */
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
    // Places score at the first rank it beats and returns that rank in the low
    // byte, or 0xff when it does not place.  The caller increments the count.
    unsigned int  insert(unsigned int score, unsigned char levelId);

    unsigned char count() const                      { return count_; }
    void          setCount(unsigned char n)          { count_ = n; }
    // The rank insert() last placed a score at.
    unsigned char lastRank() const                   { return lastRank_; }
    HighScoreRecord       *record(unsigned int i)       { return &records_[i]; }
    const HighScoreRecord *record(unsigned int i) const { return &records_[i]; }

    // Called only by the Game's construction and destruction.
    void construct();
    void destruct();
    // The default name in the first count() rows, then ten fixed score and
    // level pairs.  Its four stack arguments are ignored.
    void fillDefaults();

private:
    HighScoreTable() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(HighScoreTable);

    const void      *vtable_;  // our one-slot table
    unsigned char    lastRank_;
    HighScoreRecord  records_[RECORD_MAX];
    unsigned char    count_;
};

KAROO_LAYOUT_CHECKS(HighScoreTable)
{
    KAROO_LAYOUT_AT(lastRank_, 0x04);
    KAROO_LAYOUT_AT(records_,  0x05);
    KAROO_LAYOUT_AT(count_,    0x3705);
    KAROO_LAYOUT_SIZE(0x3706);
}

/* The one slot of HighScoreTable's vtable. */
extern "C" __declspec(dllexport) HighScoreTable *__attribute__((thiscall))
HighScoreTable_ScalarDestructor(HighScoreTable *self, unsigned char flags);
