/* FallingTile: a floor tile (kind TILE_FALLING) that drops away a moment
 * after someone stands on it and, unless its param says otherwise, comes back.
 * Every function that touches a FallingTile field is in fallingtile.cpp;
 * the fields are private.  The tile it leaves is the level's to draw; the
 * tile itself draws only the burst of dust it falls in (draw()). */

#pragma once
#include "entitycontext.h"
#include "entitylist.h"
#include "tickstep.h"
#include "drawfwd.h"

 

class SoundVoice;
class SoundLibrary;
class Tile;

class FallingTile {
public:
     

    // Spawns a falling tile; the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<FallingTile> &fallings, unsigned int uArg, unsigned int vArg,
                              unsigned int heightArg, unsigned int paramArg);

    // Releases its sounds, before World destroys it.
    void releaseSounds(SoundLibrary *sm);

    // One tick.
    void tick();

    // The two sounds, attached by InitLevelBasedSounds (levelsounds.cpp).
    void setFallSound(SoundVoice *p)    { fallSound_ = p; }
    void setRespawnSound(SoundVoice *p) { respawnSound_ = p; }

    // Set for the tick it falls.
    int  justFell() const                      { return justFell_; }

    // The tick it falls, starts a burst of dust on its cell (particles pass).
    void draw(draw::Frame &f, draw::Pass pass) const;
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }

    ~FallingTile();

private:

    FallingTile();
    FallingTile(const FallingTile &) = delete;
    FallingTile &operator=(const FallingTile &) = delete;

    // The tile it sits on: (cellU_, cellV_), read signed.
    Tile *tile() const;
    // Positions the sound at the tile and triggers it.
    void playAtTile(SoundVoice *snd, const Tile *t) const;

    double              now_;             // latched from *clock_
    double             *clock_;           // World::clock()
    TickStep        *tickStep_;           // World::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;       // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;
    float               posY_;
    float               posV_;            // negated
    signed char         cellU_;           // read by RenderGameFrame
    signed char         cellV_;
    signed char         heightCell_;
    LevelMap               *map_;        // World::map()
    int                 justRespawned_;   // set for the respawn tick
    int                 justFell_;        // set for the fall tick
    double              eventTime_;       // the clock at the last fall or respawn
    int                 respawnPending_;
    unsigned char       field_4c;
    SoundVoice *fallSound_;       // may be NULL
    SoundVoice *respawnSound_;    // may be NULL
    int                 armed_;
    int                 noRespawn_;       // the tile's param: nonzero never respawns
    double              armedAt_;
};
