/* Deliberation (docs/BOT_DESIGN.md, section 3.7): from the goals, the option
 * to take now.  For each goal, most relevant first, the planner's cheapest
 * plan (goap.h) over the board, its first step done safely by the tactical
 * layer (tactics.h, choose); if no plan's first step is safe, or only
 * another way than the one planned, the plans again with the routes going
 * round the throwers near the player.  A goal with a cell to make for
 * (Goal::toward) is taken only once its plan does it in one step; till then
 * the player approaches that cell.  With no goal doable it approaches the
 * way to finishing as far as is safe, or evades; with no plan to finish at
 * all, it fetches the gems the foes left if they are to give the rest, or
 * keeps out of their way.
 *
 * Also what the agent loop watches (Progress) and its re-check of the option
 * under way (stillSafe). */
#pragma once

#include <string>
#include <vector>
#include "goals.h"
#include "tactics.h"

namespace bot {

struct Deliberation {
    bool     midway = false;   // a script is under way: only a goal above `under` is tried
    GoalKind under = FINISH;   // the goal of the script under way
    double   mu = 0;           // the planner's weight on risk (goap.h)
    bool     tallyAll = false; // check every option, for the log (Tally)
};

struct Decision {
    bool        have = false;    // an option to take
    bool        stuck = false;   // no plan to finish at all: wait for the terrain to change
    Option      option;          // the option taken (empty if none)
    GoalKind    goal = FINISH;   // what it is for
    Plan        first;           // the plan for the goal tried first
    Plan        finish;          // the plan to finish, if planned
    std::string planned;         // the plan taken, described
    std::string note;            // the decision, or why there is none
    std::vector<Tally> tally;    // by action, then approach's and evade's (tallyName)
};

/* The option to take now for goals (deliberate.h).  Midway, with no goal
 * above the one under way doable, have is false and nothing else is to be
 * acted on. */
Decision deliberate(const BotView &view, FoeModel &foes, const Board &board,
                    const std::vector<Goal> &goals, const Deliberation &how);

/* Tally i's name in the decision log. */
const char *tallyName(size_t i);

/* What news makes the agent loop decide again: the gems or bombs gained,
 * and, while stuck, the terrain the routes go over. */
struct Progress {
    int gems = -1, bombs = -1;
    unsigned long long terrain = 0;

    static Progress of(const BotView &view);
    /* A gem or a pickup since; bombs spent are not news, and are taken in. */
    bool news(const BotView &view);
    /* While stuck: the terrain has changed, or (periodic) the foes may have
     * opened a way. */
    bool mayOpen(const BotView &view, bool periodic) const;
};

/* Whether the rest of the option's schedule ahead, and standing at its end,
 * still meets no foe or bomb. */
bool stillSafe(FoeModel &foes, const BotView &view, const std::vector<Timed> &ahead,
               const Option &option);

}  // namespace bot
