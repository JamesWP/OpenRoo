/* Game -- the one global game object (COHESION_PLAN.md Band 3).
 *
 * SKETCH.  Band 3 is last in the plan; until then this class grows one
 * accessor at a time, as each object class needs a Game field.  Only fields
 * some converted class actually reads are here.  Files not yet converted
 * still use their own G_* offsets -- when one is converted, its G_* defines
 * are deleted in favour of these.
 *
 * The layout belongs to the game (its allocator and GameTick still own it),
 * so there are no data members: every field is an offset from `this`,
 * stated once below.  Unknown fields are named field_<offset> -- naming is
 * RE work and happens in Ghidra first.
 */
#pragma once

class SoundManager;
class LiftObject;

class Game {
public:
    /* ── sound ──────────────────────────────────────────────────────── */
    SoundManager *soundManager()     { return (SoundManager *)(raw() + OFF_SOUND_MGR); }
    /* Nonzero once sound is up.  Inside the SoundManager's span, but not
     * yet confirmed as one of its fields -- so it lives here for now. */
    int  soundCreated() const        { return at<int>(OFF_SOUND_CREATED); }

    /* ── time ───────────────────────────────────────────────────────── */
    /* The 8-byte clock accumulator.  Objects keep a pointer to it and
     * re-read it every tick. */
    double        *clock()           { return (double *)(raw() + OFF_CLOCK); }
    /* An 8-byte record every level object copies to its +0x15 each tick;
     * meaning unknown. */
    unsigned char *field_170a5c()    { return raw() + OFF_170A5C; }

    /* ── tiles ──────────────────────────────────────────────────────── */
    /* The base Tile::at() indexes from.  Objects keep their own copy. */
    unsigned char *tileBase()        { return raw() + OFF_TILE_BASE; }

    /* ── lifts ──────────────────────────────────────────────────────── */
    unsigned char liftCount() const          { return at<unsigned char>(OFF_LIFT_COUNT); }
    void          setLiftCount(unsigned char n) { at<unsigned char>(OFF_LIFT_COUNT) = n; }
    LiftObject   *liftSlot(unsigned int i) const
                  { return at<LiftObject *>(OFF_LIFT_SLOTS + i * 4); }
    void          setLiftSlot(unsigned int i, LiftObject *p)
                  { at<LiftObject *>(OFF_LIFT_SLOTS + i * 4) = p; }

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */

    static const unsigned int OFF_SOUND_MGR     = 0x13cba8;
    static const unsigned int OFF_SOUND_CREATED = 0x13cc34;
    static const unsigned int OFF_CLOCK         = 0x170a54;
    static const unsigned int OFF_170A5C        = 0x170a5c;
    static const unsigned int OFF_LIFT_SLOTS    = 0x173719;   /* unaligned */
    static const unsigned int OFF_LIFT_COUNT    = 0x173b19;
    static const unsigned int OFF_TILE_BASE     = 0x2ab58d;

    unsigned char       *raw()       { return (unsigned char *)this; }
    const unsigned char *raw() const { return (const unsigned char *)this; }

    /* Many Game fields sit at odd offsets; every access is unaligned-safe. */
    template <class T> T &at(unsigned int off)
    {
        typedef T __attribute__((aligned(1))) ua;
        return *(ua *)(raw() + off);
    }
    template <class T> const T &at(unsigned int off) const
    {
        typedef T __attribute__((aligned(1))) ua;
        return *(const ua *)(raw() + off);
    }
};
