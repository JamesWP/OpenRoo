/* What the level's entities need of the game around them, so they know
 * nothing of Game: the World's clock and this tick's step, as they are now,
 * and the map.  World::entityContext() makes one for each spawn and each
 * tick; an entity keeps none of it.  The list each kind lives in
 * (entitylist.h) is passed beside it. */
#pragma once

#include "tickstep.h"

class  LevelMap;

struct EntityContext {
    double        now;   // the World's clock, ms
    TickStep      step;  // this tick's step, ms
    LevelMap     *map;
    /* The level's name (diagnostics). */
    const char     *levelName;
};
