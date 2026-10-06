/* FallingTile: a floor tile (kind TILE_FALLING) that drops away a moment
 * after someone stands on it and, unless its param says otherwise, comes back.
 * Every function that touches a FallingTile field is in fallingtile.cpp;
 * the fields are private.  RenderGameFrame reads the just-fell flag and the
 * cell bytes to place the falling-tile effect. */

#pragma once

 
#include "game.h"

namespace audiodev { class Buffer; }
class Tile;

class FallingTile {
public:
     

    // Spawns a falling tile; the arguments are masked to bytes.  PRESERVED:
    // returns idx & 0xffffff00, which no caller uses.
    static unsigned int spawn(Game *game, unsigned int uArg, unsigned int vArg,
                              unsigned int heightArg, unsigned int paramArg);

    // Destroys every falling tile and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // The two sounds, attached by InitLevelBasedSounds (levelsounds.cpp).
    void setFallSound(audiodev::Buffer *p)    { fallSound_ = p; }
    void setRespawnSound(audiodev::Buffer *p) { respawnSound_ = p; }

    // Read by RenderGameFrame to start a destruct-field burst on the tick a
    // falling tile falls: the flag, and the cell (read signed).
    int  justFell() const                      { return justFell_; }
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }

private:

    // Allocates and constructs one; NULL if the allocation fails.
    static FallingTile *create();
    FallingTile();
    virtual ~FallingTile();
    FallingTile(const FallingTile &) = delete;
    FallingTile &operator=(const FallingTile &) = delete;

    // The tile it sits on: (cellU_, cellV_), read signed.
    Tile *tile() const;
    // Positions the sound at the tile and triggers it.
    void playAtTile(audiodev::Buffer *snd, const Tile *t) const;

    double              now_;             // latched from *clock_
    double             *clock_;           // Game::clock()
    TickStep        *tickStep_;           // Game::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;       // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;
    float               posY_;
    float               posV_;            // negated
    signed char         cellU_;           // read by RenderGameFrame
    signed char         cellV_;
    signed char         heightCell_;
    LevelMap               *map_;        // Game::map()
    int                 justRespawned_;   // set for the respawn tick
    int                 justFell_;        // set for the fall tick
    double              eventTime_;       // the clock at the last fall or respawn
    int                 respawnPending_;
    unsigned char       field_4c;
    audiodev::Buffer *fallSound_;       // may be NULL
    audiodev::Buffer *respawnSound_;    // may be NULL
    int                 armed_;
    int                 noRespawn_;       // the tile's param: nonzero never respawns
    double              armedAt_;
};

/* Destroys every falling tile and zeroes the count; Game's teardown calls it. */
void Sim_PurgeFallingTiles(Game *self);
