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
class Foe;
class Player;

class __attribute__((packed)) Game {
public:
    static const int ORIGIN = 0;

    /* The game's one Game object, through its global pointer 0x0046c498
     * (NULL until the game has built it).  The only place that address is
     * named. */
    static Game *instance()          { return *(Game **)0x0046c498; }

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
    /* Gems the level requires (Player::gemsCollected is the other side). */
    int            gemsRequired() const { return gemsRequired_; }

    /* ── vitality and the score tally ───────────────────────────────── */
    /* The tally's "vitality" row, which pays it 1:1: Player::fieldD8 per
     * second of field_170a65, times 25, capped at 100 -- a RATE, not
     * health (gamestate.cpp's note: it varies with movement).  Named for
     * the row it feeds, as determinism.cpp and the manifest already do. */
    unsigned char  vitalityPercent() const           { return vitalityPercent_; }
    void           setVitalityPercent(unsigned char p) { vitalityPercent_ = p; }
    /* The divisor: GameTick adds each tick's clock step to it and the
     * level builder zeroes it, so likely elapsed level time.  Unconfirmed. */
    unsigned int   field_170a65() const              { return field_170a65_; }
    void           setField170a65(unsigned int n)    { field_170a65_ = n; }
    int            gemsScore() const                 { return gemsScore_; }
    void           setGemsScore(int n)               { gemsScore_ = n; }
    int            vitalityScore() const             { return vitalityScore_; }
    void           setVitalityScore(int n)           { vitalityScore_ = n; }

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

    /* ── foes ───────────────────────────────────────────────────────── */
    /* The same slot/count/ID-list shape as the bombs, and the same *Ref
     * accessors for the shared remove tail. */
    unsigned char  foeCount() const               { return foeCount_; }
    void           setFoeCount(unsigned char n)   { foeCount_ = n; }
    unsigned char *foeCountRef()    { return (unsigned char *)this + offsetof(Game, foeCount_); }
    unsigned char  foeId(unsigned int i) const    { return foeIds_[i]; }
    unsigned char *foeIds()         { return (unsigned char *)this + offsetof(Game, foeIds_); }
    Foe           *foeSlot(unsigned int id) const { return foeSlots_[id]; }
    Foe          **foeSlotRef(unsigned int id)
    {
        return (Foe **)((unsigned char *)this + offsetof(Game, foeSlots_)) + id;
    }
    /* A WORD the foe spawn bumps for type 0x0b; levelsetup.cpp calls it a
     * crystal count.  Not confirmed, so not named. */
    unsigned short field_42252() const             { return field_42252_; }
    void           setField42252(unsigned short n) { field_42252_ = n; }
    /* ── the player ─────────────────────────────────────────────────── */
    /* Embedded at +0x1751c9 (player.h).  A cast rather than a member:
     * player.h includes this header, through movableentity.h. */
    Player       *player()       { return reinterpret_cast<Player *>(gap_1751c9); }
    const Player *player() const { return reinterpret_cast<const Player *>(gap_1751c9); }

    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }

    /* The named sound assets the spawns and level sounds acquire: a 256-byte
     * file name with its enabled flag immediately after.  Named by offset;
     * meanings not decoded.  They sit on a 0x10c stride (0x42262 + k*0x10c
     * covers every one up to 0x42ffe), so they are likely one table of
     * 0x10c-byte records -- not yet modelled as one. */
    const SoundAssetName *soundAsset42262() const { return &soundAsset42262_; }
    const SoundAssetName *soundAsset4236e() const { return &soundAsset4236e_; }
    const SoundAssetName *soundAsset4247a() const { return &soundAsset4247a_; }
    const SoundAssetName *soundAsset42586() const { return &soundAsset42586_; }
    const SoundAssetName *soundAsset42692() const { return &soundAsset42692_; }
    const SoundAssetName *soundAsset4279e() const { return &soundAsset4279e_; }
    const SoundAssetName *soundAsset428aa() const { return &soundAsset428aa_; }
    const SoundAssetName *soundAsset429b6() const { return &soundAsset429b6_; }
    const SoundAssetName *soundAsset42ac2() const { return &soundAsset42ac2_; }
    const SoundAssetName *soundAsset42bce() const { return &soundAsset42bce_; }
    const SoundAssetName *soundAsset42cda() const { return &soundAsset42cda_; }
    const SoundAssetName *soundAsset42de6() const { return &soundAsset42de6_; }
    const SoundAssetName *soundAsset42ef2() const { return &soundAsset42ef2_; }
    const SoundAssetName *soundAsset42ffe() const { return &soundAsset42ffe_; }
    const SoundAssetName *soundAsset4310a() const { return &soundAsset4310a_; }
    const SoundAssetName *soundAsset43216() const { return &soundAsset43216_; }
    const SoundAssetName *soundAsset441ca() const { return &soundAsset441ca_; }
    const SoundAssetName *soundAsset44c42() const { return &soundAsset44c42_; }
    const SoundAssetName *soundAsset44d4e() const { return &soundAsset44d4e_; }
    const SoundAssetName *soundAsset44e5a() const { return &soundAsset44e5a_; }
    const SoundAssetName *soundAsset456ba() const { return &soundAsset456ba_; }
    const SoundAssetName *soundAsset457c6() const { return &soundAsset457c6_; }
    const SoundAssetName *soundAsset458d2() const { return &soundAsset458d2_; }
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

    unsigned char gap_000000[0x042252 - 0x000000];
    unsigned short field_42252_;                          /* 0x042252 */
    unsigned char gap_042254[0x042262 - 0x042254];
    SoundAssetName soundAsset42262_;                 /* 0x042262 */
    unsigned char gap_042366[0x04236e - 0x042366];
    SoundAssetName soundAsset4236e_;                 /* 0x04236e */
    unsigned char gap_042472[0x04247a - 0x042472];
    SoundAssetName soundAsset4247a_;                 /* 0x04247a */
    unsigned char gap_04257e[0x042586 - 0x04257e];
    SoundAssetName soundAsset42586_;                 /* 0x042586 */
    unsigned char gap_04268a[0x042692 - 0x04268a];
    SoundAssetName soundAsset42692_;                 /* 0x042692 */
    unsigned char gap_042796[0x04279e - 0x042796];
    SoundAssetName soundAsset4279e_;                 /* 0x04279e */
    unsigned char gap_0428a2[0x0428aa - 0x0428a2];
    SoundAssetName soundAsset428aa_;                 /* 0x0428aa */
    unsigned char gap_0429ae[0x0429b6 - 0x0429ae];
    SoundAssetName soundAsset429b6_;                 /* 0x0429b6 */
    unsigned char gap_042aba[0x042ac2 - 0x042aba];
    SoundAssetName soundAsset42ac2_;                 /* 0x042ac2 */
    unsigned char gap_042bc6[0x042bce - 0x042bc6];
    SoundAssetName soundAsset42bce_;                 /* 0x042bce */
    unsigned char gap_042cd2[0x042cda - 0x042cd2];
    SoundAssetName soundAsset42cda_;                 /* 0x042cda */
    unsigned char gap_042dde[0x042de6 - 0x042dde];
    SoundAssetName soundAsset42de6_;                 /* 0x042de6 */
    unsigned char gap_042eea[0x042ef2 - 0x042eea];
    SoundAssetName soundAsset42ef2_;                 /* 0x042ef2 */
    unsigned char gap_042ff6[0x042ffe - 0x042ff6];
    SoundAssetName soundAsset42ffe_;                 /* 0x042ffe */
    unsigned char gap_043102[0x04310a - 0x043102];
    SoundAssetName soundAsset4310a_;                 /* 0x04310a */
    unsigned char gap_04320e[0x043216 - 0x04320e];
    SoundAssetName soundAsset43216_;                 /* 0x043216 */
    unsigned char gap_04331a[0x0441ca - 0x04331a];
    SoundAssetName soundAsset441ca_;                 /* 0x0441ca */
    unsigned char gap_0442ce[0x044c42 - 0x0442ce];
    SoundAssetName soundAsset44c42_;                 /* 0x044c42 */
    unsigned char gap_044d46[0x044d4e - 0x044d46];
    SoundAssetName soundAsset44d4e_;                 /* 0x044d4e */
    unsigned char gap_044e52[0x044e5a - 0x044e52];
    SoundAssetName soundAsset44e5a_;                 /* 0x044e5a */
    unsigned char gap_044f5e[0x0456ba - 0x044f5e];
    SoundAssetName soundAsset456ba_;                 /* 0x0456ba */
    unsigned char gap_0457be[0x0457c6 - 0x0457be];
    SoundAssetName soundAsset457c6_;                 /* 0x0457c6 */
    unsigned char gap_0458ca[0x0458d2 - 0x0458ca];
    SoundAssetName soundAsset458d2_;                 /* 0x0458d2 */
    unsigned char gap_0459d6[0x046132 - 0x0459d6];
    SoundAssetName soundAsset46132_;                 /* 0x046132 */
    unsigned char gap_046236[0x046baa - 0x046236];
    SoundAssetName soundAsset46baa_;                 /* 0x046baa */
    unsigned char gap_046cae[0x13cba8 - 0x046cae];
    /* The SoundManager is embedded here; its full size is unknown (its
     * lists reach at least +0xa4), so only the bytes up to the next field
     * we use are declared.  soundCreated_ sits inside it at +0x8c. */
    unsigned char soundManagerHead_[0x13cc34 - 0x13cba8];
    int           soundCreated_;                          /* 0x13cc34 */
    unsigned char gap_13cc38[0x140502 - 0x13cc38];
    /* Two of the end-of-level tally's six SCORE cells (levelscore.cpp has
     * the full table), written by CalculateLevelScore 0x41a760. */
    int           gemsScore_;                             /* 0x140502 */
    unsigned char gap_140506[0x140516 - 0x140506];
    int           vitalityScore_;                         /* 0x140516 */
    unsigned char gap_14051a[0x170643 - 0x14051a];
    BridgeObject *bridgeSlots_[256];                      /* 0x170643 */
    unsigned char bridgeCount_;                           /* 0x170a43 */
    unsigned char gap_170a44[0x170a54 - 0x170a44];
    double        clock_;                                 /* 0x170a54 */
    Field170a5c   field_170a5c_;                          /* 0x170a5c */
    /* Recomputed by GameTick every tick; see vitalityPercent(). */
    unsigned char vitalityPercent_;                       /* 0x170a64 */
    unsigned int  field_170a65_;                          /* 0x170a65 */
    unsigned char gap_170a69[0x173483 - 0x170a69];
    /* Its length is unknown; declared only as far as the next field. */
    char          levelName_[0x173588 - 0x173483];        /* 0x173483 */
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
    Foe          *foeSlots_[500];                         /* 0x174804 */
    unsigned char foeCount_;                              /* 0x174fd4 */
    /* 500 long: the Player object follows at 0x1751c9. */
    unsigned char foeIds_[500];                           /* 0x174fd5 */
    unsigned char gap_1751c9[0x2ab58d - 0x1751c9];
    /* Where Tile::at() indexes from; the tiles extend past it.  The bytes
     * from here to the first cell look like a map header (the time limit at
     * +0x2ab591, this quota, the extents at +0x2ab727/8), not tile fields --
     * see COHESION_PLAN.md Band 3; not yet modelled. */
    unsigned char tileOrigin_[0x2ab723 - 0x2ab58d];       /* 0x2ab58d */
    /* The level's gem quota: CalculateLevelScore 0x41a760 pays 5 a gem up
     * to it and 10 per gem the Player collects beyond it. */
    int           gemsRequired_;                          /* 0x2ab723 */
};

KAROO_LAYOUT_CHECKS(Game)
{
    KAROO_LAYOUT_AT(soundManagerHead_, 0x13cba8);
    KAROO_LAYOUT_AT(soundCreated_,     0x13cc34);
    KAROO_LAYOUT_AT(bridgeSlots_,      0x170643);
    KAROO_LAYOUT_AT(bridgeCount_,      0x170a43);
    KAROO_LAYOUT_AT(clock_,            0x170a54);
    KAROO_LAYOUT_AT(field_170a5c_,     0x170a5c);
    KAROO_LAYOUT_AT(gemsScore_,        0x140502);
    KAROO_LAYOUT_AT(vitalityScore_,    0x140516);
    KAROO_LAYOUT_AT(vitalityPercent_,  0x170a64);
    KAROO_LAYOUT_AT(field_170a65_,     0x170a65);
    KAROO_LAYOUT_AT(slideSlots_,       0x173588);
    KAROO_LAYOUT_AT(slideCount_,       0x173718);
    KAROO_LAYOUT_AT(liftSlots_,        0x173719);
    KAROO_LAYOUT_AT(liftCount_,        0x173b19);
    KAROO_LAYOUT_AT(breakableSlots_,   0x173b1e);
    KAROO_LAYOUT_AT(breakableCount_,   0x173e3e);
    KAROO_LAYOUT_AT(soundAsset42262_,  0x042262);
    KAROO_LAYOUT_AT(soundAsset4236e_,  0x04236e);
    KAROO_LAYOUT_AT(soundAsset4247a_,  0x04247a);
    KAROO_LAYOUT_AT(soundAsset42586_,  0x042586);
    KAROO_LAYOUT_AT(soundAsset42692_,  0x042692);
    KAROO_LAYOUT_AT(soundAsset4279e_,  0x04279e);
    KAROO_LAYOUT_AT(soundAsset428aa_,  0x0428aa);
    KAROO_LAYOUT_AT(soundAsset429b6_,  0x0429b6);
    KAROO_LAYOUT_AT(soundAsset42ac2_,  0x042ac2);
    KAROO_LAYOUT_AT(soundAsset42bce_,  0x042bce);
    KAROO_LAYOUT_AT(soundAsset42cda_,  0x042cda);
    KAROO_LAYOUT_AT(soundAsset42de6_,  0x042de6);
    KAROO_LAYOUT_AT(soundAsset42ef2_,  0x042ef2);
    KAROO_LAYOUT_AT(soundAsset42ffe_,  0x042ffe);
    KAROO_LAYOUT_AT(soundAsset4310a_,  0x04310a);
    KAROO_LAYOUT_AT(soundAsset43216_,  0x043216);
    KAROO_LAYOUT_AT(soundAsset441ca_,  0x0441ca);
    KAROO_LAYOUT_AT(soundAsset44c42_,  0x044c42);
    KAROO_LAYOUT_AT(soundAsset44d4e_,  0x044d4e);
    KAROO_LAYOUT_AT(soundAsset44e5a_,  0x044e5a);
    KAROO_LAYOUT_AT(soundAsset456ba_,  0x0456ba);
    KAROO_LAYOUT_AT(soundAsset457c6_,  0x0457c6);
    KAROO_LAYOUT_AT(soundAsset458d2_,  0x0458d2);
    KAROO_LAYOUT_AT(soundAsset46132_,  0x046132);
    KAROO_LAYOUT_AT(soundAsset46baa_,  0x046baa);
    KAROO_LAYOUT_AT(bombSlots_,        0x173e3f);
    KAROO_LAYOUT_AT(bombCount_,        0x17460f);
    KAROO_LAYOUT_AT(bombIds_,          0x174610);
    KAROO_LAYOUT_AT(foeSlots_,         0x174804);
    KAROO_LAYOUT_AT(foeCount_,         0x174fd4);
    KAROO_LAYOUT_AT(foeIds_,           0x174fd5);
    KAROO_LAYOUT_AT(field_42252_,      0x042252);
    KAROO_LAYOUT_AT(levelName_,        0x173483);
    KAROO_LAYOUT_AT(tileOrigin_,       0x2ab58d);
    KAROO_LAYOUT_AT(gemsRequired_,     0x2ab723);
}
