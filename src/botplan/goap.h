/* The GOAP planner (docs/BOT_DESIGN.md, section 3.4): the cheapest sequence
 * of actions from the facts as they stand to a goal (goals.h, which also
 * chooses it):
 *
 *   finish       the player at the exit with the gem quota collected;
 *   remove foe   a foe fewer than now, by an ambush started before the foe
 *                meets the player (PlanOptions::startBy).
 *
 * The facts: where the player is (a place, board.h), the changes made to
 * the terrain (bombable blocks spent, bridges switched), the gems still
 * needed (and those gathered already, short of the quota), the bombs and glides held, the foes alive (and how many of them leave a gem
 * when killed) and the bomb pickups taken.  The actions are given: each a PlanAction, which says
 * from a state what states it leads to and at what estimated cost.  The
 * bot's are in actions.h, each with its preconditions and effects; the
 * planner knows none of them.
 *
 * The search is A* over the facts, the heuristic the quota's route from where
 * the player is and the exit in the most open world (every block spent,
 * every bridge extended, worlds.h), a lower bound since the other actions
 * only add.  Its budget is in nodes expanded and
 * worlds built, never time, so it is deterministic.
 *
 * A step's cost is time + mu * risk (section 3.4).  Its risk is the time it
 * spends among live foes, in foe-seconds; mu is in ms a foe-second.  At mu 0
 * (the default) the planner weighs time alone and leaves the foes to the
 * tactical layer.
 *
 * The estimates are checked where it matters, the first step (tactics.h,
 * choose): the planner can be told what a first step was found to take, or
 * that it cannot be had now, and the cheapest plan given that is the
 * answer. */
#pragma once

#include <string>
#include <tuple>
#include <vector>
#include "board.h"

namespace bot {

enum GoalKind { FINISH, REMOVE_FOE, GOAL_KINDS };

const char *goalName(GoalKind goal);

/* The facts a plan's state is made of. */
struct Facts {
    Place    at;
    Changes  changes = 0;
    int      gemsNeeded = 0;
    Taken    taken = 0;      // gems gathered on the way, short of the quota
    int      bombs = 0;
    int      glides = 0;
    int      foes = 0;
    int      carriers = 0;   // of the foes, those that leave a gem
    unsigned pickups = 0;    // a bit per board pickup taken (the first 32)
    bool     unused = false; // bombs taken that the plan has not used yet
    bool     atExit = false;

    auto key() const
    {
        return std::make_tuple(at.u, at.v, at.facing, changes, gemsNeeded, taken, bombs, glides, foes, carriers, pickups,
                               unused, atExit);
    }
    bool operator<(const Facts &o) const { return key() < o.key(); }
};

struct PlanStep {
    int        action = -1;        // its index among the actions planned with
    const char *name = "";         // the action's
    Place      end;                // where it ends (a pickup, the exit, a site)
    Step       heads{-1, -1};      // where the player first heads for, if anywhere
    double     ms = 0;             // its time
    double     risk = 0;           // foe-seconds
};

struct PlanOptions;

/* An action as the planner sees it: from a state, the states it leads to,
 * each with the step that gets there (its ms an estimate). */
class PlanAction {
public:
    struct Successor {
        Facts    facts;
        PlanStep step;
    };

    virtual ~PlanAction() = default;
    virtual const char *name() const = 0;
    virtual void expand(const Facts &f, const Board &board, const PlanOptions &options,
                        std::vector<Successor> &out) const = 0;
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
    /* First steps found out already: by action and end, either not to be
     * had now (ms < 0) or taking ms in fact, in place of the estimate. */
    struct Known {
        int    action;
        Place  end;
        double ms;
    };
    std::vector<Known> root;
    int      maxNodes = 4000;
    size_t   maxWorlds = 512;                // what-if terrains built, besides the base
};

/* The cheapest plan to options.goal by the actions (each expanded in their
 * order: ties go to the earlier). */
Plan planGoal(const Board &board, const std::vector<const PlanAction *> &actions,
              const PlanOptions &options);

}  // namespace bot
