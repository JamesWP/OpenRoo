/* The tactical layer (docs/BOT_DESIGN.md, section 3.5): one step of a plan
 * (goap.h) as something the player can do now, safely.
 *
 * A step becomes options: each a script for the executor (script.h), the
 * player's place over time while it runs (a schedule, nav.h), the blasts of
 * any bombs it drops and a cost.  Every option is checked against the foes,
 * the bombs and the falling tiles (danger.h), and the cheapest safe one is
 * the answer.  The options for each kind of step:
 *
 *   collect  walk to the route's next gem, or another of the nearest gems
 *            (costed with the whole route still to do);
 *   bombs    walk to the planned pickup, or another (likewise);
 *   exit     walk to the exit;
 *   ambush   turn, drop a bomb and walk away, taken only where the foe
 *            model says the blast kills a foe (ambush.h).
 * A walk is also offered after a wait, so a foe can go by first.
 *
 * And two ways to keep the player safe while no step of any plan can be
 * done: approach, the farthest safe cell along the way to a goal (waiting
 * there for a foe to come within a bomb's reach is how an ambush becomes
 * possible); and evade, a nearby cell to stand on. */
#pragma once

#include <vector>
#include "board.h"
#include "foemodel.h"
#include "goap.h"
#include "nav.h"
#include "option.h"

namespace bot {

/* What the tactics share for one decision: the view, the nav model, the
 * quickest routes from the player as it stands, and the foe model the
 * options are checked with. */
struct Situation {
    Situation(const BotView &v, FoeModel &m);

    const BotView &view;
    NavModel       nav;
    Paths          from;
    FoeModel      &foes;
};

/* How many options were offered and found safe, for the decision log.  With
 * all set, every option is checked; otherwise up to the first safe one. */
struct Tally {
    bool all = false;
    int  offered = 0, safe = 0;
};

/* The plan's first step, as the cheapest safe option: false if none. */
bool realise(Situation &s, const Board &board, const Plan &plan, Option *out, Tally *tally);

/* Part of the way to goal, as far along as is safe: false if no cell is. */
bool approach(Situation &s, Step goal, Option *out, Tally *tally);

/* A nearby cell to stand on, safe: false if none. */
bool evade(Situation &s, Option *out, Tally *tally);

}  // namespace bot
