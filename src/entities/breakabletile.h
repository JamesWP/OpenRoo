/* BreakableTile -- a floor tile that drops away a moment after someone
 * stands on it, and (unless its param says not to) comes back.  Tile kind
 * 0x0d.  Ghidra struct `BreakableTile`.
 *
 * Every function that touches a BreakableTile field is in breakabletile.cpp:
 * spawn (0x418240), tick (0x403d40) and purge (0x4183f0).  Construction and
 * destruction are ours too -- our own `new`, and our own one-slot vtable in
 * place of the game's 0x45d2d0; the originals 0x403ce0, 0x403d10 and
 * 0x403d30 are UD2-stubbed.
 *
 * One outside accessor remains, which is why the layout stays packed and
 * asserted: RenderGameFrame (original) reads the just-fell flag +0x3c and the
 * cell bytes +0x31/+0x32/+0x33 to place the falling-tile effect.
 * (levelsounds.cpp attaches the two sounds through their setters.)
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;
class Tile;

class __attribute__((packed)) BreakableTile {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnBreakableObject 0x00418240.  Arguments are dwords masked to
     * bytes, exactly as the original reads them.  Returns what the original
     * leaves in EAX, `idx & 0xffffff00`; no caller uses it. */
    static unsigned int spawn(Game *game, unsigned int uArg, unsigned int vArg,
                              unsigned int heightArg, unsigned int paramArg);

    /* Game::PurgeBreakableObjects 0x004183f0 -- destroy every breakable and
     * zero the count. */
    static void purgeAll(Game *game);

    /* UpdateBreakableTile 0x00403d40 -- one tick. */
    void tick();

    /* +0x4d / +0x51, attached by InitLevelBasedSounds (levelsounds.cpp). */
    void setFallSound(CStaticSoundbuffer *p)    { fallSound_ = p; }
    void setRespawnSound(CStaticSoundbuffer *p) { respawnSound_ = p; }

private:

    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(BreakableTile *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x403ce0 -- allocate and construct, with our own new.  NULL if
     * allocation fails, as the original's operator new returned NULL. */
    static BreakableTile *create();
    BreakableTile();
    /* 0x403d10 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(BreakableTile *self,
                                                              unsigned int flags);
    /* Destroy through the object's own vtable, flags 1, as the purge did. */
    void destroy();

    /* The tile this breakable sits on: (cellU_, cellV_), read signed. */
    Tile *tile() const;
    /* Position the sound at the tile and trigger it. */
    void playAtTile(CStaticSoundbuffer *snd, const Tile *t) const;

    KAROO_LAYOUT_REGISTER(BreakableTile);

    const Vtbl         *vtable_;         /* +0x00  &VTABLE                   */
    double              now_;            /* +0x04  latched from *clock_      */
    double             *clock_;          /* +0x0c  Game::clock()             */
    TickStep        *tickStep_;         /* +0x10  Game::tickStep()        */
    unsigned char       field_14;        /* +0x14                            */
    TickStep         tickStepCopy_;     /* +0x15  copied from *tickStep_      */
    unsigned char       field_1d[8];     /* +0x1d                            */
    float               posU_;           /* +0x25  } base-class fields,      */
    float               posY_;           /* +0x29  } zeroed by 0x401000      */
    float               posV_;           /* +0x2d  } (V is NEGATED)          */
    signed char         cellU_;          /* +0x31  } read by RenderGameFrame */
    signed char         cellV_;          /* +0x32  }                         */
    signed char         heightCell_;     /* +0x33  }                         */
    unsigned char      *tileBase_;       /* +0x34  Game::tileBase()          */
    int                 justRespawned_;  /* +0x38  set for the respawn tick  */
    int                 justFell_;       /* +0x3c  set for the fall tick; read
                                                   by RenderGameFrame        */
    double              eventTime_;      /* +0x40  clock at last fall/respawn */
    int                 respawnPending_; /* +0x48                            */
    unsigned char       field_4c;        /* +0x4c                            */
    CStaticSoundbuffer *fallSound_;      /* +0x4d  may be NULL               */
    CStaticSoundbuffer *respawnSound_;   /* +0x51  may be NULL               */
    int                 armed_;          /* +0x55                            */
    int                 noRespawn_;      /* +0x59  the tile's param byte     */
    double              armedAt_;        /* +0x5d                            */
};

KAROO_LAYOUT_CHECKS(BreakableTile)
{
    KAROO_LAYOUT_AT(now_,            0x04);
    KAROO_LAYOUT_AT(clock_,          0x0c);
    KAROO_LAYOUT_AT(tickStep_,         0x10);
    KAROO_LAYOUT_AT(tickStepCopy_,     0x15);
    KAROO_LAYOUT_AT(posU_,           0x25);
    KAROO_LAYOUT_AT(posY_,           0x29);
    KAROO_LAYOUT_AT(posV_,           0x2d);
    KAROO_LAYOUT_AT(cellU_,          0x31);
    KAROO_LAYOUT_AT(cellV_,          0x32);
    KAROO_LAYOUT_AT(heightCell_,     0x33);
    KAROO_LAYOUT_AT(tileBase_,       0x34);
    KAROO_LAYOUT_AT(justRespawned_,  0x38);
    KAROO_LAYOUT_AT(justFell_,       0x3c);
    KAROO_LAYOUT_AT(eventTime_,      0x40);
    KAROO_LAYOUT_AT(respawnPending_, 0x48);
    KAROO_LAYOUT_AT(fallSound_,      0x4d);
    KAROO_LAYOUT_AT(respawnSound_,   0x51);
    KAROO_LAYOUT_AT(armed_,          0x55);
    KAROO_LAYOUT_AT(noRespawn_,      0x59);
    KAROO_LAYOUT_AT(armedAt_,        0x5d);
    /* No size check: the object is ours to allocate, so nothing relies on
     * it being the original's 0x65. */
}

/* 0x4183f0 -- destroy every slot and zero the count; Game's destructor calls it. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBreakableObjects(Game *self);
