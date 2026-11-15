#include "routeattempt.h"
#include <stdlib.h>
#include <vector>
#include <string>
#include "kill.h"
#include "world.h"

namespace simbot {

namespace {

const int MAX_LEAD = 600;
/* Routes pressed to their end with the goal not come about, before it is
 * given up on. */
const int MAX_ROUTES = 3;

}  // namespace

Attempt::Status RouteAttempt::act(const World &live, Work &work, unsigned *presses)
{
    if (!route_) {
        if (!atRest(live))
            return Thinking;
        if (goal_->done(live))
            return Done;
        if (routes_ >= MAX_ROUTES) {
            why_ = "pressed its Routes to their end in vain";
            return Impossible;
        }
        World ahead(live);
        for (int i = 0; i < lead_; i++)
            tick(ahead, 0, &work);
        std::shared_ptr<Goal> goal = goal_;
        approach_ = false;
        // The kill, the one goal a RouteAttempt knows: each Route aimed at
        // where the foe has come to, and, while it is far, a walk near it
        // first (it may come to meet the player), then the kill from there.
        // Part 2 of docs/SIMBOT_REWORK.md has this a SequenceSearch.
        if (const KillGoal *kill = dynamic_cast<const KillGoal *>(goal_.get())) {
            const Foe *f = findFoe(ahead, kill->foe());
            if (!f)
                return Thinking;   // gone while standing: done next frame
            const Cell at{f->cellU(), f->cellV()};
            const bool moving = foeMoving(ahead, *f);
            unsigned blocker;
            if (blockedBy(*graph_, ahead, kill->foe(), at, f->heightCell(), moving, &blocker)) {
                requirement_ = foeDead(*survey_, blocker);
                why_ = "foe " + std::to_string(blocker) + " is in the way";
                return Impossible;
            }
            if (nearFoe(*graph_, placeOf(ahead), at, f->heightCell(), moving)) {
                goal = std::make_shared<KillGoal>(at, kill->foe(), *graph_);
            } else {
                goal = std::make_shared<TrackGoal>(at, kill->foe(), *graph_);
                approach_ = true;
            }
        }
        // The walk near the foe searches as a plain walk does.
        const RouteOptions o = approach_ ? RouteOptions{} : options_;
        const bool waits = waits_ && (!approach_ || approachWaits_);
        route_ = std::make_unique<Route>(ahead, goal, *graph_, o.places,
                                         waits && !ahead.foes().empty() ? o.shortWaits : 0,
                                         waits ? o.timedWaits : 0);
        at_ = 0;
    }
    const Route::Status status = route_->run(work);
    why_ = route_->summary();
    if (status == Route::NoWay) {
        route_.reset();
        if (approach_) {
            if (approachWaits_)
                return Impossible;
            approachWaits_ = true;
            return Thinking;
        }
        if (waits_)
            return Impossible;
        waits_ = true;
        return Thinking;
    }
    if (status == Route::Searching)
        return Thinking;
    const auto &p = route_->presses();
    const auto &e = route_->expects();
    if (at_ < e.size() && at_ == 0 && live.now() < e[0].now) {
        *presses = 0;   // standing till the game comes to where the Route starts
        return Pressing;
    }
    if (at_ >= p.size()) {
        route_.reset();
        if (goal_->done(live))
            return Done;
        if (!approach_)
            routes_++;
        return Thinking;
    }
    if (expectOf(live) == e[at_]) {
        *presses = p[at_++];
        return Pressing;
    }
    if (at_ == 0 && lead_ < MAX_LEAD)
        lead_ *= 2;   // the search outran its lead
    route_.reset();
    return Thinking;
}

}  // namespace simbot
