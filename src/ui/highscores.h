/* HighScoreTable: the high-score table, a sub-object of Game.  The game keeps
 * count() rows (10) of name, score and level, loaded from and saved to
 * Highscores\<name>.hsc (highscores.cpp). */
#pragma once

 

/* One row: the name the player typed, the score and the level reached. */
struct HighScoreRecord {
    char          name[0x32];
    unsigned int  score;
    unsigned char level;
};

enum { HIGH_SCORE_RECORD_BYTES = 0x37 };

class HighScoreTable {
public:
     

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

    HighScoreTable();
    virtual ~HighScoreTable();
    HighScoreTable(const HighScoreTable &) = delete;
    HighScoreTable &operator=(const HighScoreTable &) = delete;
    // The default name in the first count() rows, then ten fixed score and
    // level pairs.  Its four stack arguments are ignored.
    void fillDefaults();


private:
     

    unsigned char    lastRank_;
    HighScoreRecord  records_[RECORD_MAX];
    unsigned char    count_;
};
 