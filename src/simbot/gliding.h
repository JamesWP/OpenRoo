/* Glides worked out from the game's rules rather than played: a move in the
 * air (moves.h: FORWARD, BACK, LEFT and RIGHT a turn and a step, WAIT a
 * drift) from where the player glides, with no step under way.  The rules,
 * from movableentity.cpp:
 *
 *   - it sinks steadily, stepping or not: what each move takes (its ms and
 *     the height it sinks) is played once where the glide begins
 *     (glideCosts) and kept to;
 *   - over a tile it comes down to (within half a height of its top) it
 *     keeps to the top, and lands there once no step is under way; a tile
 *     higher than that it passes through;
 *   - a step is refused off the map, into an impassable tile or an unbombed
 *     block, or onto a tile one above it;
 *   - below height 2 the glide ends, and the fall kills.
 *
 * Foes are not counted (the MoveGraph has none).  Only an estimate for the
 * MoveGraph, which plays each landing once to know where it truly ends. */
#pragma once

#include "moves.h"

class LevelMap;
class World;

namespace simbot {

/* The player in the air: its cell, facing and height (posY). */
struct Air {
    int   u = 0, v = 0, facing = 0;
    float y = 0;
    /* The game's height cell for it. */
    int heightCell() const { return (int)y + 1; }
};
Air airOf(const World &w);

/* What each move takes in the air, as played from w. */
struct GlideCosts {
    double ms[WAIT + 1];
    float  drop[WAIT + 1];
};
GlideCosts glideCosts(const World &w);

enum class Glided { Flying, Landed, Refused, Lost };
/* Move m from a, by the rules: a is moved on (landed: on the tile's top). */
Glided glide(const LevelMap &map, Air &a, Move m, const GlideCosts &costs);

}  // namespace simbot
