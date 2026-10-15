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

#include <vector>
#include "botview.h"

namespace bot {

struct Blast {
    Step   cell{0, 0};
    int    h = 0;
    double from = 0, until = 0;   // clock ms the kill zone is live
    bool   lost = false;          // fell off the map: no blast to fear
    /* Whether an entity at (u, v), height h, at clock t is in the blast. */
    bool kills(int u, int v, int eh, double t) const;
};

/* A bomb dropped at clock droppedAt on (u, v) at height h, facing f. */
Blast predictBomb(const BotView &view, int u, int v, int h, int facing, double droppedAt);

/* A spell of drop times, clock ms, over which a drop gives the one blast
 * (its times those of a drop at the spell's start). */
struct DropSpell {
    double from = 0, to = 0;
    Blast  blast;
};
/* How long a spell must last to be counted on: a script's drop comes later
 * than planned more than earlier. */
const double DROP_SLACK_MS = 500.0;
/* The spells a drop on (u, v) at height h facing f, from clock fromMs on,
 * gives a blast in: one, open-ended (to infinite), if where it blows does not hang on when;
 * else (it rolls onto a platform or a lift) those over the carrier's next
 * cycle that last DROP_SLACK_MS, in order. */
std::vector<DropSpell> dropSpells(const BotView &view, int u, int v, int h, int facing,
                                  double fromMs);
/* The cells next to what carries a bomb on (a platform's track, a lift): a
 * drop there may blast blocks far off. */
std::vector<Step> carriedFrom(const BotView &view);

/* Whether the blast spends the block on c, if c holds one: within a cell of
 * it, at its exact height. */
bool spends(const BotView &view, const Blast &blast, Step c);
/* Whether the blast spends some bombable block of the view. */
bool spendsBlock(const BotView &view, const Blast &blast);
/* A bomb already in the view, rolling or not. */
Blast predictBomb(const BotView &view, const BombView &bomb);

}  // namespace bot
