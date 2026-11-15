/* The planner: GOAP, A* over Facts (facts.h), by the Actions' planning
 * halves, for each of its sub-goals in turn.  Nothing is played: the costs are
 * the Actions' estimates.  A sub-goal is searched for by the clock and its
 * own estimate, a tie going by its own tie-break; one other than the last
 * that cannot be met is planned to the facts with the least of it left,
 * soonest.  The steps for the last are then given to the caller's polish,
 * and kept in its order if quicker.  It runs a little each frame (work.h).
 * It knows nothing of the game: the Survey is passed to the Actions unread.
 *
 * Steps found impossible as things stand are left out (Attempt::Impossible);
 * the caller clears them when the facts change.  Steps with a Requirement
 * learned are offered only once it holds. */
#pragma once

#include <functional>
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
    double polished = 0;   // the ms the gem order's polish saved
    bool   kept = false;   // the last plan's rest, kept to

    /* "collect g3 (5,4) ~2.1s -> exit (9,9) ~1.5s". */
    std::string describe() const;
};

/* What a plan is for: left, how far the facts are from it (0: met).  A
 * search for one ends where it is met, or after maxNodes at the facts with
 * the least left, soonest (no steps, if none has less than its start). */
struct SubGoal {
    std::function<int(const Facts &)>    left;
    /* The ms still to go from the facts, any weight included; none: the
     * clock alone. */
    std::function<double(const Facts &)> estimate;
    /* Among nodes equally far, the lower first; none: 0. */
    std::function<int(const Facts &)>    tie;
    int maxNodes = 4000;
};

/* Reorders the steps a plan for the last sub-goal starts with (from start);
 * the planner offers them again in the new order, and keeps it if it is
 * quicker. */
using Polish = std::function<void(const Facts &start, std::vector<Step> &steps, Work &work)>;

using Ruled = std::set<std::pair<const Action *, int>>;
/* Requirements learned (Attempt::requiresDead): for a step, the foes (bits
 * of Facts::alive) that must be dead before it is offered. */
using Requires = std::map<std::pair<const Action *, int>, uint64_t>;

class Planner {
public:
    /* sub-goals: planned for in turn, each from where the last one's steps end;
     * the last is the level done, planned again from start if it cannot be
     * met from there.  rest: the steps left of the last plan, kept to if
     * they can still be taken, come as near each sub-goal and no new plan is
     * quicker (so the order holds from one plan to the next). */
    Planner(const Survey &survey, const Facts &start, const std::vector<SubGoal> &subGoals,
            const Ruled &out, const Requires &needs, const std::vector<Step> &rest = {},
            Polish polish = {});
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
    /* f's tie-break, in the search for the sub-goal under way. */
    int tie(const Facts &f) const;
    /* Whether the last sub-goal is met at f: nothing follows. */
    bool met(const Facts &f) const;
    /* A search for subGoals_[subGoal_] from f. */
    void begin(const Facts &f);
    /* The search for subGoals_[subGoal_] ended at node at (-1: not met). */
    void end(int at, Work &work);
    /* The steps to node at. */
    std::vector<Step> pathTo(int at) const;
    /* The least each sub-goal but the last comes to on steps from start. */
    std::vector<int> nearest(const Facts &start, const std::vector<Step> &steps) const;
    /* steps offered again from f, each by its Action and arg, into out with
     * their ms; false if one is offered no more. */
    bool reoffer(Facts f, const std::vector<Step> &steps, std::vector<Step> &out,
                 double &ms) const;

    const Survey &survey_;
    Facts         start_;
    std::vector<SubGoal> subGoals_;
    Ruled         out_;
    Requires      requires_;
    std::vector<Step> rest_;
    Polish        polish_;
    size_t        subGoal_ = 0;      // the sub-goal being planned for
    std::vector<Step> before_;       // the steps for the sub-goals before it
    double        beforeMs_ = 0;
    bool          again_ = false;    // the last sub-goal planned again from start_
    int           subGoalNodes_ = 0;     // nodes the search for it has expanded
    int           best_ = 0;         // its node with the least left, soonest
    std::vector<Node> nodes_;
    using Entry = std::tuple<double, int, int>;   // f, tie, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<Facts, double> seen_;
    Plan plan_;
    bool finished_ = false;
};

}  // namespace simbot
