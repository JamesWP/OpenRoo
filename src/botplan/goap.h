/* The GOAP planner (docs/BOT_DESIGN.md, section 3.4): the cheapest sequence
 * of actions from the facts as they stand to the goal, the player at the exit
 * with the gem quota collected.
 *
 * The facts: where the player is (a board point, board.h), the gems still
 * needed, the bombs held, the foes alive and the bomb pickups taken.  The
 * actions, macro-actions costed by the model:
 *
 *   action     preconditions                 effects
 *   Collect    gems needed, a route to them  the quota taken, at its last gem
 *   Bombs(p)   none held, a foe to use them  3 bombs, at pickup p
 *              on, p not taken
 *   Ambush     a bomb, a foe                 a foe less, a bomb less
 *   Exit       no gems needed, no bombs      at the exit
 *              taken and left unused
 *
 * Items are only a means to an end (section 1): a plan that takes bombs must
 * use one.  Collect takes the whole quota, by the route optimiser's route
 * (board.h).  An ambush's cost is a guess: it is the tactical layer's to find
 * one (tactics.h).
 *
 * The search is A* over the facts, the heuristic the quota's route from where
 * the player is and the exit, a lower bound since the other actions only add.
 * Its budget is in nodes expanded, never time, so it is deterministic.
 *
 * A step's cost is time + mu * risk (section 3.3).  Its risk is the time it
 * spends among live foes, in foe-seconds; mu is in ms a foe-second.  At mu 0
 * (the default) the planner weighs time alone and leaves the foes to the
 * tactical layer.
 *
 * The planner can be told to keep certain actions out of a plan's first
 * step: those the tactical layer has found no safe way to do now.  The
 * cheapest plan that starts some other way is then the answer. */
#pragma once

#include <string>
#include <vector>
#include "board.h"

namespace bot {

enum ActionKind { COLLECT, BOMBS, AMBUSH, EXIT, ACTION_KINDS };

const char *actionName(ActionKind kind);

struct PlanStep {
    ActionKind kind = EXIT;
    int        point = Board::PLAYER;  // where it ends: a board point, PLAYER for none
    std::vector<int> gems;             // Collect: the gems, in order
    double     ms = 0;                 // its time
    double     risk = 0;               // foe-seconds
};

struct Plan {
    bool   found = false;
    std::vector<PlanStep> steps;
    double cost = 0;    // the whole plan's time + mu * risk
    int    nodes = 0;   // expanded

    /* "Collect, Exit", for the log. */
    std::string describe() const;
};

struct PlanOptions {
    double   mu = 0;                         // ms a foe-second
    unsigned notFirst = 0;                   // a bit per ActionKind
    int      maxNodes = 4000;
};

Plan planGoal(const Board &board, const PlanOptions &options);

}  // namespace bot
