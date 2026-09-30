/* Bomb: a dropped bomb.  A 2 s fuse during which it rolls forward, then a 3x3
 * kill zone for 0.4 s.  A MovableEntity plus its own fields.  The Game's
 * second entity table holds only bombs.  RenderGameFrame reads its position,
 * dyingStarted and droppedAt, and reads and clears the debris latch, and the
 * movement code treats a bomb as a plain entity. */

#pragma once

 
#include "game.h"
#include "movableentity.h"

class CStaticSoundbuffer;
class SoundManager;
class Tile;

class Bomb : public MovableEntity {
public:
     

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
    // Allocates and constructs one; NULL if the allocation fails.
    static Bomb *create();
    Bomb();

    // The cell (u, v) from this bomb's tile base, both read signed.
    Tile *tile(int u, int v) const;
    static void acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                            CStaticSoundbuffer *Bomb::*field);
    static void releaseField(SoundManager *sm, Bomb **slot,
                             CStaticSoundbuffer *Bomb::*field);

    CStaticSoundbuffer *rollSound_;         // may be NULL
    CStaticSoundbuffer *blastSound_;        // may be NULL
    int                 blastSoundPlayed_;
    int                 zoneCleared_;
    double              droppedAt_;
};
