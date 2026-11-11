/* The planner: GOAP, A* over Facts (facts.h), by the Actions' planning
 * halves, for each of its Aims in turn.  Nothing is played: the costs are
 * the Actions' estimates.  An Aim other than the last is planned for by
 * the clock alone, to the facts with the least of it left, soonest; the
 * last (the level done) is guided by the rest of the quota (nearest gem
 * first) and the exit, timed on the MoveGraph, weighed ESTIMATE_WEIGHT
 * times over, and a tie goes to the state with more gems: only the first
 * step is acted on, so a good plan found soon beats the best one.  The order
 * of the gems the last Aim's steps collect first is then polished.  It runs
 * a little each frame (work.h).
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
struct Aim {
    std::function<int(const Facts &)> left;
    int maxNodes = 4000;
};

using Ruled = std::set<std::pair<const Action *, int>>;
/* Requirements learned (Attempt::requiresDead): for a step, the foes (bits
 * of Facts::alive) that must be dead before it is offered. */
using Requires = std::map<std::pair<const Action *, int>, uint64_t>;

class Planner {
public:
    /* aims: planned for in turn, each from where the last one's steps end;
     * the last is the level done, planned again from start if it cannot be
     * met from there.  rest: the steps left of the last plan, kept to if
     * they can still be taken, come as near each Aim and no new plan is
     * quicker (so the order holds from one plan to the next). */
    Planner(const Survey &survey, const Facts &start, const std::vector<Aim> &aims,
            const Ruled &out, const Requires &needs, const std::vector<Step> &rest = {});
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
    /* A search for aims_[aim_] from f. */
    void begin(const Facts &f);
    /* The search for aims_[aim_] ended at node at (-1: not met). */
    void end(int at, Work &work);
    /* The steps to node at. */
    std::vector<Step> pathTo(int at) const;
    /* The least each Aim but the last comes to on steps from start. */
    std::vector<int> nearest(const Facts &start, const std::vector<Step> &steps) const;
    /* The gems steps (from start) collect first put in a quicker order, by
     * random edits (one moved, a stretch turned about, two swapped) kept
     * when quicker (by the MoveGraph's times), the steps offered again in
     * it; ms is theirs.  What it saved. */
    double polish(const Facts &start, std::vector<Step> &steps, double &ms, Work &work) const;
    /* steps offered again from f, each by its Action and arg, into out with
     * their ms; false if one is offered no more. */
    bool reoffer(Facts f, const std::vector<Step> &steps, std::vector<Step> &out,
                 double &ms) const;

    const Survey &survey_;
    Facts         start_;
    std::vector<Aim> aims_;
    Ruled         out_;
    Requires      requires_;
    std::vector<Step> rest_;
    size_t        aim_ = 0;          // the Aim being planned for
    std::vector<Step> before_;       // the steps for the Aims before it
    double        beforeMs_ = 0;
    bool          again_ = false;    // the last Aim planned again from start_
    int           aimNodes_ = 0;     // nodes the search for it has expanded
    int           best_ = 0;         // its node with the least left, soonest
    std::vector<Node> nodes_;
    using Entry = std::tuple<double, int, int>;   // f, fewer gems, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<Facts, double> seen_;
    Plan plan_;
    bool finished_ = false;
};

}  // namespace simbot
