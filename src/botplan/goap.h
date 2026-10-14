/* The GOAP planner (docs/BOT_DESIGN.md, section 3.4): the cheapest sequence
 * of actions from the facts as they stand to a goal (goals.h, which also
 * chooses it):
 *
 *   finish       the player at the exit with the gem quota collected;
 *   remove foe   a foe fewer than now, by an ambush started before the foe
 *                meets the player (PlanOptions::startBy).
 *
 * The facts: where the player is (a place, board.h), which bombable blocks
 * are spent, the gems still needed, the bombs held, the foes alive and the
 * bomb pickups taken.  The actions, macro-actions costed by the model:
 *
 *   action     preconditions                 effects
 *   Collect    gems needed, a route to them  the quota taken, at its last gem
 *              in the world the facts make
 *   Bombs(p)   none held, a foe or a block   3 bombs, at pickup p
 *              to use them on, p not taken
 *   Blast(s)   a bomb, a site s that spends  blocks spent (the world changes:
 *              a block                       what is reachable, what gems
 *                                            they hide), a bomb less, at s
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
 * the player is and the exit in the world with every block spent, a lower
 * bound since the other actions only add.  Its budget is in nodes expanded and
 * worlds built, never time, so it is deterministic.
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

enum ActionKind { COLLECT, BOMBS, BLAST, AMBUSH, EXIT, ACTION_KINDS };

enum GoalKind { FINISH, REMOVE_FOE, GOAL_KINDS };

const char *goalName(GoalKind goal);

const char *actionName(ActionKind kind);

struct PlanStep {
    ActionKind kind = EXIT;
    bool       goes = false;       // it ends somewhere: end
    Place      end;                // where it ends (a pickup, the exit, a site)
    std::vector<Step> gems;        // Collect: the gems, in order
    Spent      hits = 0;           // Blast: the blocks it spends
    double     ms = 0;             // its time
    double     risk = 0;           // foe-seconds
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
    GoalKind goal = FINISH;
    double   startBy = -1;                   // ms: no step starts later (-1: no limit)
    double   mu = 0;                         // ms a foe-second
    unsigned notFirst = 0;                   // a bit per ActionKind
    int      maxNodes = 4000;
    size_t   maxWorlds = 64;                 // what-if terrains built, besides the base
};

Plan planGoal(const Board &board, const PlanOptions &options);

}  // namespace bot
