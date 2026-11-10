/* The planner: GOAP, A* over Facts (facts.h) to the level done, by the
 * Actions' planning halves.  Nothing is played: the costs are the Actions'
 * estimates, its guide the rest of the quota (nearest gem first) and the
 * exit, timed on the MoveGraph, weighed ESTIMATE_WEIGHT times over, and a
 * tie goes to the state with more gems: only the first step is acted on,
 * so a good plan found soon beats the best one.  It runs a little each
 * frame (work.h).
 *
 * Steps found impossible as things stand are left out (Attempt::Impossible);
 * the caller clears them when the facts change.  Steps with a Requirement
 * learned are offered only once it holds. */
#pragma once

#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <string>
#include <utility>
#include <vector>
#include "actions.h"

namespace simbot {

struct Plan {
    bool found = false;
    std::vector<Step> steps;
    double ms = 0;
    int    nodes = 0;
    /* "collect g3 (5,4) ~2.1s -> exit (9,9) ~1.5s". */
    std::string describe() const;
};

using Ruled = std::set<std::pair<const Action *, int>>;
/* Requirements learned (Attempt::requiresDead): for a step, the foes (bits
 * of Facts::alive) that must be dead before it is offered. */
using Requires = std::map<std::pair<const Action *, int>, uint64_t>;

class Planner {
public:
    Planner(const Survey &survey, const Facts &start, const Ruled &out, const Requires &needs,
            int maxNodes = 4000);
    /* Plans on with work; true once finished, the plan found or not. */
    bool run(Work &work);
    const Plan &plan() const { return plan_; }

private:
    struct Node {
        Facts  facts;
        double ms = 0;
        int    parent = -1;
        Step   step;
    };
    double estimate(const Facts &f) const;

    const Survey &survey_;
    Ruled         out_;
    Requires      requires_;
    int           maxNodes_;
    std::vector<Node> nodes_;
    using Entry = std::tuple<double, int, int>;   // f, fewer gems, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<Facts, double> seen_;
    Plan plan_;
    bool finished_ = false;
};

}  // namespace simbot
