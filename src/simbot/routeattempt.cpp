#include "routeattempt.h"
#include <stdlib.h>
#include <vector>
#include <string>
#include "world.h"

namespace simbot {

namespace {

const int MAX_LEAD = 600;
/* Waits in a row for a foe to pass: 2 s. */
const int FOE_WAITS = 8;
/* Timed waits in a row (schedule.h): with a lift or a platform passing
 * each height or cell, each way, a cycle or so. */
const int TIMED_WAITS = 24;
/* A kill's Routes are searched no further: it is near the foe or nowhere. */
const size_t KILL_PLACES = 3000;
/* Waits in a row for a foe to come on to a trap: 10 s. */
const int TRAP_WAITS = 40;
/* A trap's Routes: a search on the clock alone, no guide. */
const size_t TRAP_PLACES = 4000;
/* Routes pressed to their end with the goal not come about, before it is
 * given up on. */
const int MAX_ROUTES = 3;
/* blockedBy: a foe holds a place it comes to no later than this after the
 * player. */
const double HELD_MS = 500;

}  // namespace

bool RouteAttempt::done(const World &live) const
{
    const Place at = placeOf(live);
    return goal_.kill || goal_.trap ? foeDead(live, goal_.foe) : at.cell() == goal_.cell && !at.air;
}

bool RouteAttempt::blockedBy(const World &w, const Goal &goal, int foeH, bool moving,
                             unsigned *foe) const
{
    const MoveGraph &g = *graph_;
    const int from = g.find(placeOf(w));
    if (from < 0)
        return false;
    // Each other foe's time to every place, on the player's MoveGraph: it
    // holds a place it comes to no later than the player.
    std::vector<std::pair<unsigned, MoveGraph::Table>> others;
    for (const Foe &o : w.foes()) {
        if (o.id() == goal.foe || o.moveState() != 0)
            continue;
        const int at = g.find({o.cellU(), o.cellV(), o.heightCell(), 0});
        if (at >= 0)
            others.push_back({o.id(), g.times(at)});
    }
    const MoveGraph::Table mineTable = g.times(from);
    const std::vector<double> &mine = *mineTable;
    auto heldBy = [&](int p, unsigned *id) {
        for (const auto &[oid, t] : others)
            if ((*t)[p] >= 0 && (mine[p] < 0 || (*t)[p] <= mine[p] + HELD_MS)) {
                *id = oid;
                return true;
            }
        return false;
    };
    unsigned id;
    auto near = [&](int p) { return nearFoe(g, g.place(p), goal.cell, foeH, moving); };
    auto held = [&](int p) { return heldBy(p, &id); };
    if (!g.path(from, near, held).empty())
        return false;
    for (int p : g.path(from, near, [](int) { return false; }))
        if (p != from && heldBy(p, foe))
            return true;
    return false;   // no way even without them: the search will find so
}

Attempt::Status RouteAttempt::act(const World &live, Work &work, unsigned *presses)
{
    if (!route_) {
        if (!atRest(live))
            return Thinking;
        if (done(live))
            return Done;
        if (routes_ >= MAX_ROUTES) {
            why_ = "pressed its Routes to their end in vain";
            return Impossible;
        }
        World ahead(live);
        for (int i = 0; i < lead_; i++)
            tick(ahead, 0, &work);
        Goal goal = goal_;
        approach_ = false;
        if (goal.kill) {
            const Foe *f = findFoe(ahead, goal.foe);
            if (!f)
                return Thinking;   // gone while standing: done next frame
            goal.cell = {f->cellU(), f->cellV()};
            unsigned blocker;
            if (blockedBy(ahead, goal, f->heightCell(), foeMoving(ahead, *f), &blocker)) {
                requirement_ = foeDead(*survey_, blocker);
                why_ = "foe " + std::to_string(blocker) + " is in the way";
                return Impossible;
            }
            if (!nearFoe(*graph_, placeOf(ahead), goal.cell, f->heightCell(),
                         foeMoving(ahead, *f))) {
                // Far: walk near it first (it may come to meet the player),
                // then the kill from there.
                goal.kill = false;
                goal.track = true;
                approach_ = true;
            }
        }
        const bool waits = waits_ && (!approach_ || approachWaits_);
        route_ = std::make_unique<Route>(ahead, goal, *graph_,
                                         goal.kill ? KILL_PLACES : goal.trap ? TRAP_PLACES : 6000,
                                         waits && !ahead.foes().empty() ? (goal.trap ? TRAP_WAITS : FOE_WAITS) : 0,
                                         waits ? TIMED_WAITS : 0);
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
        if (done(live))
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
