/* The danger check: does a timed plan of the player's (nav.h's schedule) meet
 * a foe or a bomb?
 *
 * The foes are run forward with the foe model (foemodel.h), chasing the player
 * along the plan and then while it stands at the plan's end for a margin.
 * At each tick: a catcher on the player's cell, or swapping cells with it,
 * is a catch; standing beside a thrower (within one cell, diagonals too) is
 * what makes it drop a bomb, and counts as a hit; so does standing in a
 * bomb's blast (bombmodel.h), of a bomb in the view or one the caller means
 * to drop, or one a foe drops along the way; and so does still standing on a falling tile when it falls, 1.5 s
 * after it was stepped on (or armed already). */
#pragma once

#include <vector>
#include "botview.h"
#include "foemodel.h"
#include "nav.h"

namespace bot {

struct Danger {
    bool        hit = false;
    double      t = 0;      // ms from now
    const char *why = "";   // "catcher", "thrower", "bomb", "falling tile", "bridge"
    int         killed = 0; // foes the extra blasts killed meanwhile
    std::vector<FoeModel::Kill> kills;  // those, where and when (the foe model's clock)
};

/* plan's times are ms from the view's clock; the player stays on the last
 * cell for lingerMs after the last entry.  extra: blasts of bombs the player
 * means to drop. */
Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs, const std::vector<Blast> &extra = {});

/* How soon a foe (a catcher, or a thrower's reach) meets the player on plan
 * and then standing at its end, within horizonMs: ms from now, or -1. */
double foeMeets(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                double horizonMs);

}  // namespace bot
