/* The planner, GOAP: the cheapest sequence of actions from a World to a goal,
 * cost being ms of play.
 *
 * Its states are Worlds: each is the one before with an action played out on
 * a copy, so an action's preconditions and effects are whatever the game
 * makes of it, never a model of the game.  The facts the search goes by are
 * read off each World (Facts): where the player stands and which of the
 * start's gems are taken.  A state whose facts were come to sooner is not
 * searched again.  The search is A*, the goal's estimate its heuristic (a
 * guide, not a bound: the plans are good rather than the quickest), with
 * a budget in states expanded, so it is deterministic.
 *
 * An action offers, from a state, the states it leads to (Action::expand),
 * each with the presses that get there.  The actions share what is costly to
 * work out once per state (Planning): the quickest ways about from it
 * (reach.h). */
#pragma once

#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <tuple>
#include <vector>
#include "reach.h"

class World;

namespace simbot {

/* What the planner shares with the actions while one plan is made. */
class Planning {
public:
    explicit Planning(const World &start);
    ~Planning();

    /* The cells the start's gems lie on, in grid order. */
    const std::vector<Cell> &gems() const { return gems_; }
    Cell exit() const { return exit_; }
    /* The least ms from rest on one cell to another (a gem's, the exit's,
     * the start's) on the level as it started: what the estimates go by.
     * Found by a reach from where the start's reach first came to the
     * cell, once for each.  < 0 if there is no way. */
    double legMs(Cell from, Cell to);

    /* The state about to be expanded: what follows is of it. */
    void begin(const World &w);
    /* The quickest ways about from it, the Worlds on the gems and the exit
     * kept: worked out when first asked for. */
    const Reach &reach();

private:
    using Key = std::pair<int, int>;
    std::vector<Cell> targets() const;

    std::vector<Cell> gems_;
    Cell              exit_;
    Cell              start_;
    std::map<Key, std::unique_ptr<World>> samples_;    // the player at rest on the cell
    std::map<Key, std::map<Key, double>>  legs_;       // from a cell, to each target
    const World      *from_ = nullptr;
    std::unique_ptr<Reach> reach_;
};

/* The facts a state is searched by. */
struct Facts {
    Place    at;
    uint64_t taken = 0;   // a bit per gem of Planning::gems (the first 64)
    auto key() const { return std::make_tuple(at.key(), taken); }
    bool operator<(const Facts &o) const { return key() < o.key(); }
};

Facts factsOf(const World &w, const Planning &planning);

class Action {
public:
    struct Successor {
        std::unique_ptr<World> world;
        std::string            what;      // for the note: "gem (3,4)"
        std::vector<unsigned>  presses;   // each tick's, from the state
    };

    virtual ~Action() = default;
    virtual const char *name() const = 0;
    virtual void expand(const World &from, Planning &planning, std::vector<Successor> &out) const = 0;
};

class Goal {
public:
    virtual ~Goal() = default;
    virtual const char *name() const = 0;
    virtual bool reached(const World &w) const = 0;
    /* An estimate of the ms from w to the goal. */
    virtual double estimate(const World &w, Planning &planning) const = 0;
};

struct PlanStep {
    const char *action = "";
    std::string what;
    double      ms = 0;     // its own
};

struct Plan {
    bool found = false;
    std::vector<PlanStep> steps;
    std::vector<unsigned> presses;   // every tick's, from the start
    double ms = 0;
    int    expanded = 0;

    /* "gem (3,4), gem (5,1), exit (9,9)". */
    std::string describe() const;
};

/* The cheapest plan to the goal by the actions, expanding at most
 * maxStates states. */
Plan makePlan(const World &start, const std::vector<const Action *> &actions, const Goal &goal,
              int maxStates = 200);

}  // namespace simbot
