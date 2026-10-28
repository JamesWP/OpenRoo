/* Bomb: a dropped bomb.  A 2 s fuse during which it rolls forward, then a 3x3
 * kill zone for 0.4 s.  A MovableEntity plus its own fields.  The World's
 * bomb list holds only bombs.  It draws itself (draw()), and the
 * movement code treats a bomb as a plain entity. */

#pragma once

 
#include <cstddef>
#include "entitycontext.h"
#include "entitylist.h"
#include "movableentity.h"
#include "drawfwd.h"

class Tile;

class Bomb : public MovableEntity {
public:
     

    // Spawns a bomb at (u, v, h), reporting it created; the arguments
    // are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<Bomb> &bombs, PlayEvents &events,
                      unsigned int uArg, unsigned int vArg, unsigned int hArg,
                      unsigned int flagArg);

    // Destroys bombs[i], reporting it destroyed; those after it move
    // down one.
    static void remove(EntityList<Bomb> &bombs, std::size_t i, PlayEvents &events);

    // One tick: the fuse, the roll and the blast.
    void tick(PlayEvents &events);

    // Draws it on the debug map; n is its place in the World's bomb list.
    void debugDraw(unsigned n) const;

    // Draws the bomb for one pass of the frame: the bomb, or once it has
    // gone off the explosion (firing its debris), and its particles.
    // deathsDrawn is the level's death count at the last frame drawn: a
    // newer death fires its effects.
    void draw(draw::Frame &f, draw::Pass pass, unsigned deathsDrawn) const;

    // When it was dropped, in clock ms; it blows 2 s later.  Read by the bot.
    double droppedAt() const { return droppedAt_; }

private:
    Bomb();

    // The cell (u, v) from this bomb's tile base, both read signed.
    Tile *tile(int u, int v) const;
    int                 blastSoundPlayed_;
    int                 zoneCleared_;
    double              droppedAt_;
};
