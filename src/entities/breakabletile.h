/* BreakableTile: a floor tile (kind TILE_BREAKABLE) that drops away a moment
 * after someone stands on it and, unless its param says otherwise, comes back.
 * Every function that touches a BreakableTile field is in breakabletile.cpp;
 * the fields are private.  The layout is fixed: RenderGameFrame reads the
 * just-fell flag and the cell bytes to place the falling-tile effect. */

#pragma once

 
#include "game.h"

class CStaticSoundbuffer;
class Tile;

class __attribute__((packed)) BreakableTile {
public:
     

    // Spawns a breakable; the arguments are masked to bytes.  PRESERVED:
    // returns idx & 0xffffff00, which no caller uses.
    static unsigned int spawn(Game *game, unsigned int uArg, unsigned int vArg,
                              unsigned int heightArg, unsigned int paramArg);

    // Destroys every breakable and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // The two sounds, attached by InitLevelBasedSounds (levelsounds.cpp).
    void setFallSound(CStaticSoundbuffer *p)    { fallSound_ = p; }
    void setRespawnSound(CStaticSoundbuffer *p) { respawnSound_ = p; }

    // Read by RenderGameFrame to start a destruct-field burst on the tick a
    // breakable falls: the flag, and the cell (read signed).
    int  justFell() const                      { return justFell_; }
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }

private:

    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).
    struct Vtbl {
        void *(  *scalarDeletingDtor)(BreakableTile *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static BreakableTile *create();
    BreakableTile();
    // Vtable slot 0.
    static void *  scalarDeletingDtor(BreakableTile *self,
                                                              unsigned int flags);
    // Destroys through the object's own vtable, flags 1.
    void destroy();

    // The tile it sits on: (cellU_, cellV_), read signed.
    Tile *tile() const;
    // Positions the sound at the tile and triggers it.
    void playAtTile(CStaticSoundbuffer *snd, const Tile *t) const;

     

    const Vtbl         *vtable_;          // +0x00  &VTABLE
    double              now_;             // +0x04  latched from *clock_
    double             *clock_;           // +0x0c  Game::clock()
    TickStep        *tickStep_;           // +0x10  Game::tickStep()
    unsigned char       field_14;         // +0x14
    TickStep         tickStepCopy_;       // +0x15  copied from *tickStep_
    unsigned char       field_1d[8];      // +0x1d
    float               posU_;            // +0x25
    float               posY_;            // +0x29
    float               posV_;            // +0x2d  negated
    signed char         cellU_;           // +0x31  read by RenderGameFrame
    signed char         cellV_;           // +0x32
    signed char         heightCell_;      // +0x33
    Tile               *tileBase_;        // +0x34  Game::tileBase()
    int                 justRespawned_;   // +0x38  set for the respawn tick
    int                 justFell_;        // +0x3c  set for the fall tick
    double              eventTime_;       // +0x40  the clock at the last fall or respawn
    int                 respawnPending_;  // +0x48
    unsigned char       field_4c;         // +0x4c
    CStaticSoundbuffer *fallSound_;       // +0x4d  may be NULL
    CStaticSoundbuffer *respawnSound_;    // +0x51  may be NULL
    int                 armed_;           // +0x55
    int                 noRespawn_;       // +0x59  the tile's param: nonzero never respawns
    double              armedAt_;         // +0x5d
};