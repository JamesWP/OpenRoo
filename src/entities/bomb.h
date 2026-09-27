/* Bomb: a dropped bomb.  A 2 s fuse during which it rolls forward, then a 3x3
 * kill zone for 0.4 s.  A MovableEntity (0x15a bytes) plus its own 0x18.  The
 * Game's second entity table holds only bombs.  The layout is fixed:
 * RenderGameFrame reads its position, +0x82 and droppedAt, and reads and
 * writes +0x7a, and the movement code treats a bomb as a plain entity. */

#pragma once

#include "layout.h"
#include "game.h"
#include "movableentity.h"

struct CStaticSoundbuffer;
class SoundManager;
class Tile;

class __attribute__((packed)) Bomb : public MovableEntity {
public:
    static const int ORIGIN = 0;

    // Spawns a bomb at (u, v, h); the arguments are masked to bytes.
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int hArg, unsigned int flagArg);

    // Releases the sounds, destroys the bomb and removes its ID.  PRESERVED:
    // leaves the slot dangling.
    static void remove(Game *game, unsigned int idArg);

    // One tick: the fuse, the roll and the blast.
    void tick();

    // The clock when it was dropped; RenderGameFrame switches its model 2 s
    // later.
    double droppedAt() const { return droppedAt_; }

private:
    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).  The remove destroys through it.
    struct Vtbl {
        void *(*scalarDeletingDtor)(Bomb *self,
                                    unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static Bomb *create();
    Bomb();
    // Vtable slot 0.
    static void *scalarDeletingDtor(Bomb *self,
                                    unsigned int flags);

    // The cell (u, v) from this bomb's tile base, both read signed.
    Tile *tile(int u, int v) const;
    static void acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                            CStaticSoundbuffer *Bomb::*field);
    static void releaseField(SoundManager *sm, Bomb **slot,
                             CStaticSoundbuffer *Bomb::*field);

    KAROO_LAYOUT_REGISTER(Bomb);

    CStaticSoundbuffer *rollSound_;         // +0x15a  may be NULL
    CStaticSoundbuffer *blastSound_;        // +0x15e  may be NULL
    int                 blastSoundPlayed_;  // +0x162
    int                 zoneCleared_;       // +0x166
    double              droppedAt_;         // +0x16a
};

KAROO_LAYOUT_CHECKS(Bomb)
{
    // The base sits at 0; its own fields are asserted in MovableEntity.
    KAROO_LAYOUT_AT(posU_,             0x025);
    KAROO_LAYOUT_AT(rollSound_,        0x15a);
    KAROO_LAYOUT_AT(blastSound_,       0x15e);
    KAROO_LAYOUT_AT(blastSoundPlayed_, 0x162);
    KAROO_LAYOUT_AT(zoneCleared_,      0x166);
    KAROO_LAYOUT_AT(droppedAt_,        0x16a);
    // The allocation size: the class tiles it exactly.
    KAROO_LAYOUT_SIZE(0x172);
}

/* The bomb remove, for callers outside the class. */
void Sim_RemoveEnemyObject(Game *self, unsigned int idArg);
