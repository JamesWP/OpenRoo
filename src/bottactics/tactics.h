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
 * And the options the fallbacks (fallback.h) keep the player safe with while
 * no step of any plan can be done: approach, the farthest safe cell along
 * the way to a goal (waiting there for a foe to come within a bomb's reach
 * is how an ambush becomes possible); fetching a gem; and evade, a nearby
 * cell to stand on.  The situations a decision may be planned in besides
 * the plain one are the variants (variant.h). */
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
    /* The player may be mid-move: its routes start when the move ends.
     * With aroundThrowers, they go round the throwers near the player
     * (nav.h): for when no way past them is safe. */
    Situation(const BotView &v, FoeModel &m, bool aroundThrowers = false);

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

/* The cells nearer c than any foe, in steps over the ground the player can
 * walk (the foes as they stand), up to most of them: a pocket the foes hem
 * in has few. */
int room(const Situation &s, Step c, int most);
/* Where the gems are had from the foes, the bot stays out of the pockets
 * they would hem it in: an option ending on a cell costs this much more for
 * each cell short of ROOM_CELLS it has room for. */
const int ROOM_CELLS = 40;
const double ROOM_MS = 100;
double cramped(const Situation &s, Step c);

/* The cheapest of options found safe (danger.h), sorting them: false if
 * none is; tally counts them. */
bool pick(Situation &s, std::vector<Option> &options, Option *out, Tally *tally);

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

/* Part of the way to goal, as far along as may be, and not to a cell the
 * quota and the exit cannot be had from (unchecked options). */
void offerApproach(Situation &s, const Board &board, Step goal, std::vector<Option> &out);
/* The same, the cheapest safe: false if no cell is. */
bool approach(Situation &s, const Board &board, Step goal, Option *out, Tally *tally);

/* The walk to the nearest gem lying where the player can walk to, if there
 * is one (unchecked). */
void offerFetchGem(Situation &s, const Board &board, std::vector<Option> &out);

/* Nearby cells to stand on, not traps (unchecked). */
void offerEvade(Situation &s, const Board &board, std::vector<Option> &out);

}  // namespace bot
