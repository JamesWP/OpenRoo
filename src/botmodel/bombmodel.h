/* The bomb model: where a bomb rolls, and where and when it blows.
 *
 * Written from bomb.cpp and the movement code.  On the tick after it is
 * dropped a bomb takes one step along its facing (200 ms), so it lies in front
 * of whoever dropped it, unless a wall, an unspent bombable block or a climb
 * of one without stairs stops it; down a drop it falls.  Occupants do not
 * stop it.  Once it has moved it is held and moves no more, except that ice
 * carries it on the way it went.  At 2 s, for 0.4 s
 * it blasts the 3x3 block around its cell: anything there within one height
 * of the bomb dies, foe or player, and a bombable block at its exact height is
 * spent.  A bomb that rolls onto a platform parked at the end of its track
 * rides it, as the player does (movableentity.cpp), and blows wherever the
 * platform has carried it; onto the track with the platform away, it falls.
 * A lift it rolls onto (parked at its height or below) carries it up or down
 * the same way.  botrun.h's bomb log measures how often the model is right
 * (tools/bot/bombcheck.py). */
#pragma once

#include "botview.h"

namespace bot {

struct Blast {
    Step   cell{0, 0};
    int    h = 0;
    double from = 0, until = 0;   // clock ms the kill zone is live
    bool   lost = false;          // fell off the map: no blast to fear
    bool   timed = false;         // onto a platform or a lift: where it blows, if at all, hangs on when it was dropped
    /* Whether an entity at (u, v), height h, at clock t is in the blast. */
    bool kills(int u, int v, int eh, double t) const;
};

/* A bomb dropped at clock droppedAt on (u, v) at height h, facing f. */
Blast predictBomb(const BotView &view, int u, int v, int h, int facing, double droppedAt);
/* Whether the blast spends a bombable block. */
bool spendsBlock(const BotView &view, const Blast &blast);
/* A bomb already in the view, rolling or not. */
Blast predictBomb(const BotView &view, const BombView &bomb);

}  // namespace bot
