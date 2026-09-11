/* Game -- the one global game object (COHESION_PLAN.md Band 3).
 *
 * SKETCH.  Band 3 is last in the plan; until then this class grows one
 * field at a time, as each object class needs one.  Files not yet converted
 * still use their own G_* defines -- when one is converted, its defines are
 * deleted in favour of these accessors.
 *
 * Real fields at the game's offsets, packed; everything unknown is a gap
 * whose size is written as END - START of its neighbours.  Only the offsets
 * are asserted (layout.h), never the gaps, so splitting a gap for a new
 * field changes no existing assertion.  Unknown-meaning fields are named
 * field_<offset>; naming is RE work and happens in Ghidra first.
 */
#pragma once

#include "layout.h"

class SoundManager;
class LiftObject;

/* The 8-byte record at Game+0x170a5c, copied into every level object's +0x15
 * each tick.  Meaning unknown; a struct so that it copies by assignment. */
struct Field170a5c {
    unsigned char bytes[8];
};

class __attribute__((packed)) Game {
public:
    static const int ORIGIN = 0;

    /* ── sound ──────────────────────────────────────────────────────── */
    SoundManager *soundManager()     { return (SoundManager *)soundManagerHead_; }
    /* Nonzero once sound is up. */
    int  soundCreated() const        { return soundCreated_; }

    /* ── time ───────────────────────────────────────────────────────── */
    /* The 8-byte clock accumulator.  Objects keep a pointer to it and
     * re-read it every tick.  Addressed via offsetof rather than &clock_,
     * which GCC flags for a packed member (-Waddress-of-packed-member); the
     * offset still comes from the field, and 0x170a54 is 4-aligned. */
    double        *clock()
    {
        return (double *)((unsigned char *)this + offsetof(Game, clock_));
    }
    /* An 8-byte record every level object copies to its +0x15 each tick. */
    Field170a5c   *field_170a5c()    { return &field_170a5c_; }

    /* ── tiles ──────────────────────────────────────────────────────── */
    /* The base Tile::at() indexes from.  Objects keep their own copy. */
    unsigned char *tileBase()        { return tileOrigin_; }

    /* ── lifts ──────────────────────────────────────────────────────── */
    unsigned char liftCount() const              { return liftCount_; }
    void          setLiftCount(unsigned char n)  { liftCount_ = n; }
    LiftObject   *liftSlot(unsigned int i) const { return liftSlots_[i]; }
    void          setLiftSlot(unsigned int i, LiftObject *p) { liftSlots_[i] = p; }

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(Game);

    unsigned char gap_000000[0x13cba8 - 0x000000];
    /* The SoundManager is embedded here; its full size is unknown (its
     * lists reach at least +0xa4), so only the bytes up to the next field
     * we use are declared.  soundCreated_ sits inside it at +0x8c. */
    unsigned char soundManagerHead_[0x13cc34 - 0x13cba8];
    int           soundCreated_;                          /* 0x13cc34 */
    unsigned char gap_13cc38[0x170a54 - 0x13cc38];
    double        clock_;                                 /* 0x170a54 */
    Field170a5c   field_170a5c_;                          /* 0x170a5c */
    unsigned char gap_170a64[0x173719 - 0x170a64];
    LiftObject   *liftSlots_[256];                        /* 0x173719 */
    unsigned char liftCount_;                             /* 0x173b19 */
    unsigned char gap_173b1a[0x2ab58d - 0x173b1a];
    /* Where Tile::at() indexes from; the tiles extend past it. */
    unsigned char tileOrigin_[1];                         /* 0x2ab58d */
};

KAROO_LAYOUT_CHECKS(Game)
{
    KAROO_LAYOUT_AT(soundManagerHead_, 0x13cba8);
    KAROO_LAYOUT_AT(soundCreated_,     0x13cc34);
    KAROO_LAYOUT_AT(clock_,            0x170a54);
    KAROO_LAYOUT_AT(field_170a5c_,     0x170a5c);
    KAROO_LAYOUT_AT(liftSlots_,        0x173719);
    KAROO_LAYOUT_AT(liftCount_,        0x173b19);
    KAROO_LAYOUT_AT(tileOrigin_,       0x2ab58d);
}
