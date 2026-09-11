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
class SlideObject;
class BridgeObject;
class BreakableTile;

/* The 8-byte record at Game+0x170a5c, copied into every level object's +0x15
 * each tick.  Meaning unknown; a struct so that it copies by assignment. */
struct Field170a5c {
    unsigned char bytes[8];
};

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct __attribute__((packed)) SoundAssetName {
    char name[256];
    int  enabled;
};

class Bomb;

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

    /* ── slides ─────────────────────────────────────────────────────── */
    unsigned char slideCount() const               { return slideCount_; }
    void          setSlideCount(unsigned char n)   { slideCount_ = n; }
    SlideObject  *slideSlot(unsigned int i) const  { return slideSlots_[i]; }
    void          setSlideSlot(unsigned int i, SlideObject *p) { slideSlots_[i] = p; }

    /* ── bridges ────────────────────────────────────────────────────── */
    /* Indexed by the bridge's switch slot, not the count; the count is a
     * running total (see BridgeObject::spawn). */
    unsigned char bridgeCount() const              { return bridgeCount_; }
    void          setBridgeCount(unsigned char n)  { bridgeCount_ = n; }
    BridgeObject *bridgeSlot(unsigned int i) const { return bridgeSlots_[i]; }
    void          setBridgeSlot(unsigned int i, BridgeObject *p) { bridgeSlots_[i] = p; }

    /* ── breakable tiles ────────────────────────────────────────────── */
    unsigned char  breakableCount() const              { return breakableCount_; }
    void           setBreakableCount(unsigned char n)  { breakableCount_ = n; }
    BreakableTile *breakableSlot(unsigned int i) const { return breakableSlots_[i]; }
    void           setBreakableSlot(unsigned int i, BreakableTile *p) { breakableSlots_[i] = p; }

    /* ── bombs (the game's "enemy" table) ───────────────────────────── */
    /* Slots are indexed by ID; the ID list holds the live IDs, count long.
     * The *Ref accessors hand out addresses because the remove's tail
     * (objectremove.cpp) edits them in place, and the removes re-read the
     * slot through its address exactly as the original does. */
    unsigned char  bombCount() const               { return bombCount_; }
    unsigned char *bombCountRef()   { return (unsigned char *)this + offsetof(Game, bombCount_); }
    unsigned char  bombId(unsigned int i) const    { return bombIds_[i]; }
    unsigned char *bombIds()        { return (unsigned char *)this + offsetof(Game, bombIds_); }
    Bomb          *bombSlot(unsigned int id) const { return bombSlots_[id]; }
    Bomb         **bombSlotRef(unsigned int id)
    {
        return (Bomb **)((unsigned char *)this + offsetof(Game, bombSlots_)) + id;
    }

    /* The named sound assets a bomb acquires at spawn: a 256-byte file name
     * with its enabled flag immediately after.  Meanings not decoded. */
    const SoundAssetName *soundAsset429b6() const { return &soundAsset429b6_; }
    const SoundAssetName *soundAsset42ac2() const { return &soundAsset42ac2_; }
    const SoundAssetName *soundAsset457c6() const { return &soundAsset457c6_; }
    const SoundAssetName *soundAsset46132() const { return &soundAsset46132_; }
    const SoundAssetName *soundAsset46baa() const { return &soundAsset46baa_; }

    /* ── game code still called ─────────────────────────────────────── */
    /* PLACEHOLDER: ClaimSpareObjectIdSlot 0x00417250, __thiscall on Game.
     * Ghidra types it void, but both spawns read AL as the new ID -- see
     * objectspawn.cpp for why it is called through rather than rewritten. */
    unsigned char claimSpareObjectId(unsigned char *ids, unsigned char *count)
    {
        typedef unsigned char (__attribute__((thiscall)) *fn)(Game *, unsigned char *,
                                                              unsigned char *);
        return ((fn)0x00417250)(this, ids, count);
    }

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(Game);

    unsigned char gap_000000[0x0429b6 - 0x000000];
    SoundAssetName soundAsset429b6_;                      /* 0x0429b6 */
    unsigned char gap_042aba[0x042ac2 - 0x042aba];
    SoundAssetName soundAsset42ac2_;                      /* 0x042ac2 */
    unsigned char gap_042bc6[0x0457c6 - 0x042bc6];
    SoundAssetName soundAsset457c6_;                      /* 0x0457c6 */
    unsigned char gap_0458ca[0x046132 - 0x0458ca];
    SoundAssetName soundAsset46132_;                      /* 0x046132 */
    unsigned char gap_046236[0x046baa - 0x046236];
    SoundAssetName soundAsset46baa_;                      /* 0x046baa */
    unsigned char gap_046cae[0x13cba8 - 0x046cae];
    /* The SoundManager is embedded here; its full size is unknown (its
     * lists reach at least +0xa4), so only the bytes up to the next field
     * we use are declared.  soundCreated_ sits inside it at +0x8c. */
    unsigned char soundManagerHead_[0x13cc34 - 0x13cba8];
    int           soundCreated_;                          /* 0x13cc34 */
    unsigned char gap_13cc38[0x170643 - 0x13cc38];
    BridgeObject *bridgeSlots_[256];                      /* 0x170643 */
    unsigned char bridgeCount_;                           /* 0x170a43 */
    unsigned char gap_170a44[0x170a54 - 0x170a44];
    double        clock_;                                 /* 0x170a54 */
    Field170a5c   field_170a5c_;                          /* 0x170a5c */
    unsigned char gap_170a64[0x173588 - 0x170a64];
    SlideObject  *slideSlots_[100];                       /* 0x173588 */
    unsigned char slideCount_;                            /* 0x173718 */
    LiftObject   *liftSlots_[256];                        /* 0x173719 */
    unsigned char liftCount_;                             /* 0x173b19 */
    unsigned char gap_173b1a[0x173b1e - 0x173b1a];
    BreakableTile *breakableSlots_[200];                  /* 0x173b1e */
    unsigned char breakableCount_;                        /* 0x173e3e */
    Bomb         *bombSlots_[500];                        /* 0x173e3f */
    unsigned char bombCount_;                             /* 0x17460f */
    unsigned char bombIds_[500];                          /* 0x174610 */
    unsigned char gap_174804[0x2ab58d - 0x174804];
    /* Where Tile::at() indexes from; the tiles extend past it. */
    unsigned char tileOrigin_[1];                         /* 0x2ab58d */
};

KAROO_LAYOUT_CHECKS(Game)
{
    KAROO_LAYOUT_AT(soundManagerHead_, 0x13cba8);
    KAROO_LAYOUT_AT(soundCreated_,     0x13cc34);
    KAROO_LAYOUT_AT(bridgeSlots_,      0x170643);
    KAROO_LAYOUT_AT(bridgeCount_,      0x170a43);
    KAROO_LAYOUT_AT(clock_,            0x170a54);
    KAROO_LAYOUT_AT(field_170a5c_,     0x170a5c);
    KAROO_LAYOUT_AT(slideSlots_,       0x173588);
    KAROO_LAYOUT_AT(slideCount_,       0x173718);
    KAROO_LAYOUT_AT(liftSlots_,        0x173719);
    KAROO_LAYOUT_AT(liftCount_,        0x173b19);
    KAROO_LAYOUT_AT(breakableSlots_,   0x173b1e);
    KAROO_LAYOUT_AT(breakableCount_,   0x173e3e);
    KAROO_LAYOUT_AT(soundAsset429b6_,  0x0429b6);
    KAROO_LAYOUT_AT(soundAsset42ac2_,  0x042ac2);
    KAROO_LAYOUT_AT(soundAsset457c6_,  0x0457c6);
    KAROO_LAYOUT_AT(soundAsset46132_,  0x046132);
    KAROO_LAYOUT_AT(soundAsset46baa_,  0x046baa);
    KAROO_LAYOUT_AT(bombSlots_,        0x173e3f);
    KAROO_LAYOUT_AT(bombCount_,        0x17460f);
    KAROO_LAYOUT_AT(bombIds_,          0x174610);
    KAROO_LAYOUT_AT(tileOrigin_,       0x2ab58d);
}
