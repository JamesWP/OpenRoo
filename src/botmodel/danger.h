/* The danger check: does a timed plan of the player's (nav.h's schedule) meet
 * a foe or a bomb?
 *
 * The foes are run forward with the foe model (foemodel.h), chasing the player
 * along the plan and then while it stands at the plan's end for a margin.
 * At each tick: a catcher on the player's cell, or swapping cells with it,
 * is a catch; so is standing in a bomb's blast (bombmodel.h), of a bomb in
 * the view, one the caller means to drop or one a foe drops along the way
 * (a thrower drops one when the player or, a follower, a catcher is within
 * a cell of it).  A thrower does no harm by touch, but the player cannot
 * step into its cell, which goes against the plan, and standing within its
 * reach at the plan's end, where it will drop bomb after bomb, is a hit.
 * So is still standing on a falling tile when it falls, 1.5 s after it was
 * stepped on (or armed already), or on a bridge's deck when it goes (the
 * foe model has a foe work its switch).  On a lift the player's height is
 * the lift's. */
#pragma once

#include <vector>
#include "botview.h"
#include "foemodel.h"
#include "nav.h"

namespace bot {

struct Danger {
    /* What the hit is: caught, in a thrower's reach, a thrower in the way,
     * penned in, in a blast, on a falling tile as it falls or onto one
     * fallen, on a deck as it goes. */
    enum Why { NONE, CATCHER, THROWER, BLOCKED, PENNED, BOMB, FALLING_TILE, FALLEN_TILE, BRIDGE };

    bool        hit = false;
    double      t = 0;      // ms from now
    Step        at{-1, -1}; // the player's cell then
    Why         why = NONE;
    int         killed = 0; // foes the extra blasts killed meanwhile
    std::vector<FoeModel::Kill> kills;  // those, where and when (the foe model's clock)
    int         foeKills = 0;  // foes the other blasts killed (a thrower's, say)
};

/* plan's times are ms from the view's clock; the player stays on the last
 * cell for lingerMs after the last entry.  extra: blasts of bombs the player
 * means to drop. */
Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs, const std::vector<Blast> &extra = {});

/* checkPlan for a way clear of a bomb: the player may stop within a
 * thrower's reach, only the bombs it drops meanwhile judging that (the next
 * choice keeps clear of the one after). */
Danger checkEscape(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                   double lingerMs);

/* How soon a foe (a catcher, or a thrower's reach: where it will drop a
 * bomb) meets the player on plan and then standing at its end, within
 * horizonMs: ms from now, or -1. */
double foeMeets(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                double horizonMs);

}  // namespace bot
