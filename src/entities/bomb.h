/* Bomb -- a dropped bomb: a 2 s fuse during which it rolls forward, then a
 * 3x3 kill zone for 0.4 s.  Ghidra struct `Bomb`, 0x172 bytes: a
 * MovableEntity (movableentity.h, 0x15a bytes) plus its own 0x18.
 *
 * The game's "enemy" table (Game+0x173e3f, count +0x17460f, IDs +0x174610)
 * holds nothing else: SpawnBombObject is its only filler, so the table is
 * the bomb table and RemoveEnemyObject is the bomb's remove.
 *
 * Everything is ours (bomb.cpp): spawn (0x417700), tick (0x402870), remove
 * (0x417a20), and construction and destruction -- our own `new`, and our own
 * one-slot vtable in place of the game's 0x45d29c; the originals 0x402700,
 * 0x402840 and 0x402860 are UD2-stubbed.
 *
 * Outside accessors remain, which is why the layout stays packed and
 * asserted: the original RenderGameFrame reads +0x25/+0x29/+0x2d, +0x82 and
 * +0x16a and reads AND WRITES +0x7a (listing 004280DA..00428203,
 * 00428D1D..00428D5D); worldstate.cpp reads the table by raw offset;
 * UpdateEntityMovement (movableentity.cpp) treats a bomb as a plain entity.
 */
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

    /* Game::SpawnBombObject 0x00417700.  Dword arguments masked to bytes. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int hArg, unsigned int flagArg);

    /* Game::RemoveEnemyObject 0x00417a20 -- release the sounds, destroy
     * through the vtable, compact the ID list.  Leaves the slot DANGLING. */
    static void remove(Game *game, unsigned int idArg);

    /* Game::UpdateBombFuseAndBlast 0x00402870 -- one tick. */
    void tick();

private:
    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory).  The remove
     * dispatches through it (Object_DestroyAndCompactId). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(Bomb *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x402700 -- allocate and construct, with our own new.  NULL if
     * allocation fails, as the original's operator new returned NULL. */
    static Bomb *create();
    Bomb();
    /* 0x402840 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(Bomb *self,
                                                              unsigned int flags);

    /* The cell (u, v) from this bomb's tile base, both read signed. */
    Tile *tile(int u, int v) const;
    static void acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                            CStaticSoundbuffer *Bomb::*field);
    static void releaseField(SoundManager *sm, Bomb **slot,
                             CStaticSoundbuffer *Bomb::*field);

    KAROO_LAYOUT_REGISTER(Bomb);

    CStaticSoundbuffer *rollSound_;        /* +0x15a  may be NULL            */
    CStaticSoundbuffer *blastSound_;       /* +0x15e  may be NULL            */
    int                 blastSoundPlayed_; /* +0x162                         */
    int                 zoneCleared_;      /* +0x166                         */
    double              droppedAt_;        /* +0x16a  read by RenderGameFrame */
};

KAROO_LAYOUT_CHECKS(Bomb)
{
    /* The base sits at 0 (its own fields are asserted in MovableEntity). */
    KAROO_LAYOUT_AT(posU_,             0x025);
    KAROO_LAYOUT_AT(rollSound_,        0x15a);
    KAROO_LAYOUT_AT(blastSound_,       0x15e);
    KAROO_LAYOUT_AT(blastSoundPlayed_, 0x162);
    KAROO_LAYOUT_AT(zoneCleared_,      0x166);
    KAROO_LAYOUT_AT(droppedAt_,        0x16a);
    /* The original's operator_new(0x172): the class tiles it exactly. */
    KAROO_LAYOUT_SIZE(0x172);
}

/* The bomb remove, Game::RemoveEnemyObject 0x417a20 (bomb.cpp). */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveEnemyObject(Game *self, unsigned int idArg);
