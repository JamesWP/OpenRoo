/* Session: the play-through's progress, owned by Game.  The game file's
 * level list and the level being played, the restart count, the counters
 * and timers the level score is computed from, the end-of-level tally and
 * the total play time.
 *
 * It reads no other part of Game; Game_Boot loads the game file into it
 * (gameboot.cpp), and session.cpp holds that loader.
 */
#pragma once

/* The end-of-level score tally.  Six rows, each
 * with a real COUNT and SCORE (CalculateLevelScore) and a SHOWN
 * count and score that AnimateScoreTallyStages counts up from zero
 * towards them.  The animation's stages visit FOES before TIME. */
enum TallyRow {
    TALLY_GEMS, TALLY_SURPLUS, TALLY_TIME, TALLY_FOES, TALLY_ALLITEMS,
    TALLY_VITALITY, TALLY_ROWS
};

struct ScoreTally {
    int           shownScore[TALLY_ROWS];
    int           shownBase;                /* running total before this level */
    int           shownCount[TALLY_ROWS];
    int           shownLevelTotal;          /* sum of shownScore */
    int           shownGrandTotal;          /* shownBase + shownLevelTotal */
    int           score[TALLY_ROWS];
    int           count[TALLY_ROWS];
    int           levelTotal;
    int           grandTotal;
    unsigned char stage;                    /* animation state, 0..6 */
    int           stageStart;               /* ms, the stage's start */
};

class Session {
public:
    /* ── the level sequence ─────────────────────────────────────────── */
    /* The level being played, 0-based (StoreGameStateIntoSaveSlot saves it),
     * and how many levels the game file lists (loadGameFile; zero when it
     * did not load).  index + 1 == count is the last level. */
    unsigned char  levelIndex() const                { return levelIndex_; }
    void           setLevelIndex(unsigned char i)    { levelIndex_ = i; }
    unsigned char  levelCount() const                { return levelCount_; }

    /* Total play time over the whole game, ms: each level's timeElapsed is
     * added as it ends; the save slot stores it, ClearGameState zeroes it. */
    double         totalPlayTime() const             { return totalPlayTime_; }
    void           setTotalPlayTime(double ms)       { totalPlayTime_ = ms; }

    char          *levelNameTableEntry(unsigned char i) { return levelNameTable_[i]; }

    /* The NEXT level's bonus flag, which OpenLevelFile peeks by
     * loading that level's map first.  SetupLevelObjects reads it to give
     * the completed-level menu node (0x28) two children when it is 0 and
     * one otherwise -- the builder called it a "mode flag". */
    unsigned int   nextLevelBonus() const            { return nextLevelBonus_; }
    void           setNextLevelBonus(unsigned int b) { nextLevelBonus_ = b; }

    /* ── the tally's inputs (CalculateLevelScore) ────────────────────── */
    /* Death restarts on this level: GameTick adds one each time ENTER
     * restarts it after a death, and zeroes it when the level ends (as do
     * ClearGameState and the level-skip cheat).  While it is nonzero the
     * restart is a RETRY, not a fresh level: SetupLevelObjects keeps the
     * kill and item counters, OpenLevelFile skips the bonus peek, the
     * music and extra-object sounds are not reloaded, and
     * CalculateLevelScore denies the all-items bonus.  A byte; it wraps. */
    unsigned char  restartCount() const              { return restartCount_; }
    void           setRestartCount(unsigned char n)  { restartCount_ = n; }
    /* Items the level holds (SetupLevelObjects copies its census total);
     * the all-items bonus needs Player::itemsCollected to reach it. */
    unsigned short itemTotal() const                 { return itemTotal_; }
    void           setItemTotal(unsigned short n)    { itemTotal_ = n; }
    unsigned char  foesKilled() const                { return foesKilled_; }
    void           setFoesKilled(unsigned char n)    { foesKilled_ = n; }
    /* ── vitality and the score tally ───────────────────────────────── */
    /* The tally's "vitality" row, which pays it 1:1:
     * Player::completionNumerator per second of field_170a65, times 25, capped
     * at 100 -- a RATE, not health (gamestate.cpp's note: it varies with
     * movement).  Named for the row it feeds. */
    unsigned char  vitalityPercent() const           { return vitalityPercent_; }
    void           setVitalityPercent(unsigned char p) { vitalityPercent_ = p; }
    /* The divisor: GameTick adds each tick's clock step to it and the
     * level builder zeroes it, so likely elapsed level time.  Unconfirmed. */
    unsigned int   field_170a65() const              { return field_170a65_; }
    void           setField170a65(unsigned int n)    { field_170a65_ = n; }
    /* The end-of-level tally (CalculateLevelScore fills it,
     * AnimateScoreTallyStages counts it up). */
    ScoreTally    *tally()                           { return &tally_; }
    const ScoreTally *tally() const                  { return &tally_; }
    /* Set to 1 when the tally has finished counting up (or ENTER skipped
     * it); GameTick only takes ENTER to the high-score table once it is. */
    int            tallyDone() const                 { return tallyDone_; }
    void           setTallyDone(int d)               { tallyDone_ = d; }

    /* Reads the game file <gameDir>/<name>.gam: the level names and the
     * level count.  Returns 0 when the file cannot be opened. */
    int  loadGameFile(const char *gameDir, const char *name);

private:
    /* 256 level names, 0x100 each.  SetCurrentLevelName copies entry `levelNo & 0xff` into World's levelName_. */
    char          levelNameTable_[256][0x100]{};
    unsigned char levelCount_{};
    unsigned char restartCount_{};
    /* Foes killed this level; CalculateLevelScore pays 50 each. */
    unsigned char foesKilled_{};
    unsigned short itemTotal_{};
    double        totalPlayTime_{};
    /* Recomputed by GameTick every tick; see vitalityPercent(). */
    unsigned char vitalityPercent_{};
    unsigned int  field_170a65_{};
    unsigned char levelIndex_{};
    ScoreTally    tally_;
    int           tallyDone_{};
    unsigned int  nextLevelBonus_{};
};
