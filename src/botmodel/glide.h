/* The glide model: where a paraglider takes the player (movableentity.cpp).
 *
 * Holding a glide, the player who steps off an edge into a drop of more than
 * two falls freely until it has dropped that far, then the glider opens (one
 * glide spent) and it sinks at a steady rate.  While gliding it can step and
 * turn as on the ground, at its step time; it comes down on the cell below it
 * once it stands still there and has sunk to that cell's top.  A step in the
 * air is refused by a wall or a block, and by a cell whose top is just above
 * the player, and no other: a glide passes over lower cells and void, and
 * through a cell that stands well above it (the game lets it), never
 * stopping there.
 *
 * A glide is launched from rest by a step forward off an edge.  A drop of
 * three or four only comes down on the cell below, the glider saving the
 * player from the fall; a longer one can end on any plain cell it reaches
 * above in time: the landing time is fixed by the
 * cell's height alone, since the player sinks at the same rate, moving or
 * not.  The model counts on its outcomes only with margins (heights, the
 * opening time), being unmeasured yet. */
#pragma once

#include <vector>
#include "botview.h"
#include "nav.h"

namespace bot {


/* The landings of a glide launched from rest on (u, v) facing f, a step
 * forward off the edge: one for each cell it can end on, the quickest way
 * there.  Empty if the step is no launch (no drop of three or more ahead). */
std::vector<Landing> glideLandings(const BotView &view, int u, int v, int f);

/* Whether a launch from (u, v) facing f could be: the step forward drops far
 * enough to open the glider. */
bool glideLaunch(const BotView &view, int u, int v, int f);

}  // namespace bot
