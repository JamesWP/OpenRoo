/* FallingTile: a floor tile (kind TILE_FALLING) that drops away a moment
 * after someone stands on it and, unless its param says otherwise, comes back.
 * Every function that touches a FallingTile field is in fallingtile.cpp;
 * the fields are private.  The tile it leaves is the level's to draw; the
 * burst of dust it falls in is entitydraw.cpp's. */

#pragma once
#include "entitycontext.h"
#include "entitylist.h"
#include "playevents.h"
#include "tickstep.h"

 

class Tile;

class FallingTile : public Listed {
public:
     

    // Spawns a falling tile; the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<FallingTile> &fallings, unsigned int uArg, unsigned int vArg,
                              unsigned int heightArg, unsigned int paramArg);

    // One tick; its sounds as it falls and comes back play through events.
    void tick(PlayEvents &events);

    // Set for the tick it falls.
    int  justFell() const                      { return justFell_; }

    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }
    // Armed (someone stood on it) and when, in clock ms: it falls
    // FALL_DELAY_MS after.  Read by the bot.
    bool   armed() const                       { return armed_ != 0; }
    double armedAt() const                     { return armedAt_; }
    // Fallen and waiting to come back; it never does if noRespawn().
    bool   respawnPending() const              { return respawnPending_ != 0; }
    bool   noRespawn() const                   { return noRespawn_ != 0; }

    ~FallingTile();

private:

    FallingTile();
    FallingTile(const FallingTile &) = delete;
    FallingTile &operator=(const FallingTile &) = delete;

    // The tile it sits on: (cellU_, cellV_), read signed.
    Tile *tile() const;
    // Plays one of its sounds at the tile.
    void playAtTile(PlayEvents &events, Sfx sfx, const Tile *t) const;

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
    int                 armed_;
    int                 noRespawn_;       // the tile's param: nonzero never respawns
    double              armedAt_;
};
