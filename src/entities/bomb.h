/* Bomb -- a dropped bomb: a 2 s fuse during which it rolls forward, then a
 * 3x3 kill zone for 0.4 s.  Ghidra struct `Bomb`, 0x172 bytes.
 *
 * The game's "enemy" table (Game+0x173e3f, count +0x17460f, IDs +0x174610)
 * holds nothing else: SpawnBombObject is its only filler, so the table is
 * the bomb table and RemoveEnemyObject is the bomb's remove.
 *
 * Every function of ours that touches a Bomb field is in bomb.cpp: spawn
 * (0x417700), tick (0x402870) and remove (0x417a20).
 *
 * NOT ours, flagged (COHESION_PLAN.md template 6, "flag rather than force"):
 *   - construction and destruction.  The ctor 0x402700 layers on
 *     PopulateMovableEntityBase, the base shared with foes and the player,
 *     and the dtor 0x402840 -> 0x402860 tail-calls the entity base dtor
 *     0x438760.  Taking them means taking the entity base.  So the bomb is
 *     allocated from the game's heap and destroyed through its own vtable
 *     (0x45d29c, one slot -- the next dword is 0).
 *   - outside readers: the original RenderGameFrame reads +0x25/+0x29/+0x2d,
 *     +0x82 and +0x16a and reads AND WRITES +0x7a (listing 004280DA..
 *     00428203, 00428D1D..00428D5D); worldstate.cpp reads the table by raw
 *     offset; UpdateEntityMovement (entitymove.cpp) treats a bomb as a plain
 *     entity.  So the layout stays packed and asserted.
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;
class SoundManager;
class Tile;

class __attribute__((packed)) Bomb {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnBombObject 0x00417700.  Dword arguments masked to bytes. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int hArg, unsigned int flagArg);

    /* Game::RemoveEnemyObject 0x00417a20 -- release the sounds, destroy
     * through the vtable, compact the ID list.  Leaves the slot DANGLING. */
    static void remove(Game *game, unsigned int idArg);

    /* Game::UpdateBombFuseAndBlast 0x00402870 -- one tick. */
    void tick();

    /* +0x7e, read by GameTick after the tick: nonzero = remove me.  Set by
     * UpdateEntityMovement when moveState reaches 4. */
    int removeRequested() const { return removeRequested_; }

private:
    KAROO_LAYOUT_REGISTER(Bomb);

    /* The cell (u, v) from this bomb's tile base, both read signed. */
    Tile *tile(int u, int v) const;
    static void acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                            CStaticSoundbuffer *Bomb::*field);
    static void releaseField(SoundManager *sm, Bomb **slot,
                             CStaticSoundbuffer *Bomb::*field);

    void               *vtable_;           /* +0x000  game's 0x45d29c        */
    double              now_;              /* +0x004  latched from *clock_   */
    double             *clock_;            /* +0x00c  Game::clock()          */
    Field170a5c        *record_;           /* +0x010  Game::field_170a5c()   */
    unsigned char       facing_;           /* +0x014  the spawn's flag arg   */
    Field170a5c         recordCopy_;       /* +0x015  copied from *record_   */
    unsigned char       field_1d[0x25 - 0x1d];
    float               posU_;             /* +0x025  } read by RenderGameFrame */
    float               posY_;             /* +0x029  }                      */
    float               posV_;             /* +0x02d  }                      */
    signed char         cellU_;            /* +0x031                         */
    signed char         cellV_;            /* +0x032                         */
    signed char         heightCell_;       /* +0x033  read SIGNED (MOVSX)    */
    unsigned char      *tileBase_;         /* +0x034  Game::tileBase()       */
    unsigned char       field_38[0x7e - 0x38];
    int                 removeRequested_;  /* +0x07e                         */
    unsigned char       field_82[0x86 - 0x82];
    int                 loopHalted_;       /* +0x086  1 once the blast starts */
    unsigned char       field_8a[0xab - 0x8a];
    CStaticSoundbuffer *sound_ab_;         /* +0x0ab                         */
    CStaticSoundbuffer *sound_af_;         /* +0x0af                         */
    CStaticSoundbuffer *sound_b3_;         /* +0x0b3                         */
    CStaticSoundbuffer *sound_b7_;         /* +0x0b7                         */
    CStaticSoundbuffer *sound_bb_;         /* +0x0bb                         */
    unsigned char       field_bf[0xc3 - 0xbf];
    CStaticSoundbuffer *sound_c3_;         /* +0x0c3                         */
    unsigned char       field_c7[0xef - 0xc7];
    int                 field_ef;          /* +0x0ef                         */
    unsigned char       field_f3[0x11e - 0xf3];
    unsigned char       field_11e;         /* +0x11e                         */
    unsigned char       moveState_;        /* +0x11f  4 = despawn            */
    unsigned char       field_120[0x145 - 0x120];
    unsigned char       pendingMove_;      /* +0x145                         */
    unsigned char       field_146[0x14e - 0x146];
    int                 field_14e;         /* +0x14e                         */
    unsigned char       field_152[0x15a - 0x152];
    CStaticSoundbuffer *rollSound_;        /* +0x15a  may be NULL            */
    CStaticSoundbuffer *blastSound_;       /* +0x15e  may be NULL            */
    int                 blastSoundPlayed_; /* +0x162                         */
    int                 zoneCleared_;      /* +0x166                         */
    double              droppedAt_;        /* +0x16a  read by RenderGameFrame */
};

KAROO_LAYOUT_CHECKS(Bomb)
{
    KAROO_LAYOUT_AT(now_,              0x004);
    KAROO_LAYOUT_AT(clock_,            0x00c);
    KAROO_LAYOUT_AT(record_,           0x010);
    KAROO_LAYOUT_AT(facing_,           0x014);
    KAROO_LAYOUT_AT(recordCopy_,       0x015);
    KAROO_LAYOUT_AT(posU_,             0x025);
    KAROO_LAYOUT_AT(posY_,             0x029);
    KAROO_LAYOUT_AT(posV_,             0x02d);
    KAROO_LAYOUT_AT(cellU_,            0x031);
    KAROO_LAYOUT_AT(cellV_,            0x032);
    KAROO_LAYOUT_AT(heightCell_,       0x033);
    KAROO_LAYOUT_AT(tileBase_,         0x034);
    KAROO_LAYOUT_AT(removeRequested_,  0x07e);
    KAROO_LAYOUT_AT(loopHalted_,       0x086);
    KAROO_LAYOUT_AT(sound_ab_,         0x0ab);
    KAROO_LAYOUT_AT(sound_af_,         0x0af);
    KAROO_LAYOUT_AT(sound_b3_,         0x0b3);
    KAROO_LAYOUT_AT(sound_b7_,         0x0b7);
    KAROO_LAYOUT_AT(sound_bb_,         0x0bb);
    KAROO_LAYOUT_AT(sound_c3_,         0x0c3);
    KAROO_LAYOUT_AT(field_ef,          0x0ef);
    KAROO_LAYOUT_AT(field_11e,         0x11e);
    KAROO_LAYOUT_AT(moveState_,        0x11f);
    KAROO_LAYOUT_AT(pendingMove_,      0x145);
    KAROO_LAYOUT_AT(field_14e,         0x14e);
    KAROO_LAYOUT_AT(rollSound_,        0x15a);
    KAROO_LAYOUT_AT(blastSound_,       0x15e);
    KAROO_LAYOUT_AT(blastSoundPlayed_, 0x162);
    KAROO_LAYOUT_AT(zoneCleared_,      0x166);
    KAROO_LAYOUT_AT(droppedAt_,        0x16a);
    /* Relied on: the game allocates it, operator_new(0x172) in the spawn. */
    KAROO_LAYOUT_SIZE(0x172);
}
