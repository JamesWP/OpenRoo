/* The game's rules the bot's models share: constants measured from the
 * engine (from the file named), and the tile classes they turn on.  One
 * copy each, so a change in the engine is one change here. */
#pragma once

#include <stdlib.h>
#include "botview.h"

namespace bot {

/* A move this soon after the last ends starts, on the clock, where the last
 * ended (player.cpp's stepGrace_). */
const double STEP_GRACE_MS = 20.0;
/* A step along a slide, and a sticky pad's hold (movableentity.cpp). */
const double SLIDE_MS  = 150.0;
const double STICKY_MS = 3000.0;
/* Each of a teleporter's two holds, before the jump and after. */
const double TELEPORT_MS = 500.0;
/* A falling tile: from armed (stepped on) to fallen, and to back if it comes
 * back (fallingtile.cpp). */
const double FALL_MS = 1500.0;
const double BACK_MS = 5000.0;

/* The stairs' kinds, TILE_STAIRS_1 to 4 (tile.h). */
inline bool stairs(int kind)
{
    return kind >= 5 && kind <= 8;
}

/* How many cells apart, a diagonal step counting one. */
inline int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

}  // namespace bot
