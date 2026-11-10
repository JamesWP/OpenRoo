/* Bomb: a dropped bomb.  A 2 s fuse during which it rolls forward, then a 3x3
 * kill zone for 0.4 s.  A MovableEntity plus its own fields.  The World's
 * bomb list holds only bombs.  entitydraw.cpp draws it, and the
 * movement code treats a bomb as a plain entity. */

#pragma once

 
#include <cstddef>
#include "entitycontext.h"
#include "entitylist.h"
#include "movableentity.h"

class Tile;

class Bomb : public MovableEntity {
public:
     

    // From when it is dropped: it blows FUSE_MS after, its blast is gone
    // CLEAR_MS after, and it is removed REMOVE_MS after.
    static constexpr double FUSE_MS   = 2000.0;
    static constexpr double CLEAR_MS  = 2400.0;
    static constexpr double REMOVE_MS = 2600.0;

    // Spawns a bomb at (u, v, h), reporting it created; the arguments
    // are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<Bomb> &bombs,
                      unsigned int uArg, unsigned int vArg, unsigned int hArg,
                      unsigned int flagArg);

    // Destroys bombs[i], reporting it destroyed; those after it move
    // down one.
    static void remove(EntityList<Bomb> &bombs, std::size_t i, PlayEvents &events);

    // One tick: the fuse, the roll and the blast.
    void tick(const EntityContext &ctx, PlayEvents &events);


    // When it was dropped, in clock ms; it blows 2 s later.  Read by the bot.
    double droppedAt() const { return droppedAt_; }

private:
    Bomb();

    int                 blastSoundPlayed_;
    int                 zoneCleared_;
    double              droppedAt_;
};
