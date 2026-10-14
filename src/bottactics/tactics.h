/* The tactical layer (docs/BOT_DESIGN.md, section 3.5): one step of a plan
 * (goap.h) as something the player can do now, safely.
 *
 * A step becomes options: each a script for the executor (script.h), the
 * player's place over time while it runs (a schedule, nav.h), the blasts of
 * any bombs it drops and a cost.  Every option is checked against the foes,
 * the bombs and the falling tiles (danger.h), and the cheapest safe one is
 * the answer.  The options each step offers are its action's (action.h);
 * the shared ways to build them are walks (walk.h) and drops (drops.h).  A
 * walk is also offered after a wait, so a foe can go by first.
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
    /* The player may be mid-move: its routes start when the move ends. */
    Situation(const BotView &v, FoeModel &m);

    const BotView &view;
    NavModel       nav;
    double         busyMs;   // until the move under way ends
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

/* What choose found: the planner's first plan, on its estimates; the plan
 * taken; and the option that does its first step. */
struct Choice {
    Plan   first;
    Plan   taken;
    Option option;
};

/* The cheapest plan to options' goal whose first step can be done safely
 * now, by the actions (action.h).  The planner's estimates are checked only
 * where they are acted on: the cheapest plan's first step is realised; one
 * that cannot be is ruled out, and one whose option says what the step
 * takes in fact (Option::stepMs) is costed so; then the planner is asked
 * again, until the cheapest plan starts with a step checked already.  A few
 * checks at most; tally: one per action.  False if no plan's first step can
 * be done. */
bool choose(Situation &s, const Board &board, PlanOptions options, Choice *out,
            std::vector<Tally> &tally);

/* Part of the way to goal, as far along as is safe, and not to a cell the
 * quota and the exit cannot be had from: false if no cell is. */
bool approach(Situation &s, const Board &board, Step goal, Option *out, Tally *tally);

/* A nearby cell to stand on, safe and not a trap: false if none. */
bool evade(Situation &s, const Board &board, Option *out, Tally *tally);

}  // namespace bot
