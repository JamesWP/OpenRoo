/* The danger check: does a timed plan of the player's (nav.h's schedule) meet
 * a foe or a bomb?
 *
 * The foes are run forward with the foe model (foemodel.h), chasing the player
 * along the plan and then while it stands at the plan's end for a margin.
 * At each tick: a catcher on the player's cell, or swapping cells with it,
 * is a catch; standing beside a thrower (within one cell, diagonals too) is
 * what makes it drop a bomb, and counts as a hit; so does being within two
 * cells of a live bomb before it has blown. */
#pragma once

#include <vector>
#include "botview.h"
#include "foemodel.h"
#include "nav.h"

namespace bot {

struct Danger {
    bool        hit = false;
    double      t = 0;      // ms from now
    const char *why = "";   // "catcher", "thrower", "bomb"
};

/* plan's times are ms from the view's clock; the player stays on the last
 * cell for lingerMs after the last entry. */
Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs);

}  // namespace bot
