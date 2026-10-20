/* The glide model: where a paraglider takes the player (movableentity.cpp).
 *
 * Holding a glide, the player who steps off an edge falls freely until its
 * cell is three below the edge, then the glider opens (one glide spent) and
 * it sinks at a steady rate.  While gliding it can step and turn as on the
 * ground, at its step time; it comes down on the cell below it once it stands
 * still there and its cell is that cell's top, or at the end of a move during
 * which it sank to within half a height under the top.  A step in the air is
 * refused by a wall or a block, and while the player's cell is one under the
 * top of the cell ahead; off a lift, while the player is not within 0.2 of
 * its cell's height; and otherwise not: a glide passes over lower cells and
 * void, and through a cell that stands higher (the game lets it).
 *
 * It is the game's tick by tick, in its float arithmetic: the free fall by
 * the whole ms since it began, then the sinking by the tick's step, which the
 * integrator runs once a tick and once more on a tick a move's key is
 * handled, so a move sinks the player a tick's worth more.  A move refused is
 * held, as the motor holds its key (motor.h), until the game takes it.  A
 * glide is launched from rest by a step forward off an edge onto ground that
 * does not move, and passes over a lift only out of reach of its top, and
 * over no platform's track. */
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
