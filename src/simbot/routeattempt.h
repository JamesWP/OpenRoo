/* RouteAttempt: the Attempt most Actions share: a Route to a Goal
 * (routing.h), then its presses, each only while the game is where the
 * Route expected; if it is not, a new Route from where it is, once at rest.
 *
 * A search takes frames, and the game goes on meanwhile, the player standing.
 * Standing goes the same on a copy, so the Route is searched from a copy
 * stood lead ticks ahead, and the player stands until the game gets there;
 * a search that takes longer than its lead leaves the game past its start,
 * and the next has twice the lead.
 *
 * Waiting multiplies a Route's places, so a walk is searched first without;
 * only if there is no way does it search again with waits.  A kill waits
 * from the first, for the foe to come on. */
#pragma once

#include <memory>
#include "actions.h"
#include "movegraph.h"
#include "moves.h"
#include "routing.h"

namespace simbot {

class RouteAttempt : public Attempt {
public:
    RouteAttempt(Goal goal, const MoveGraph &graph)
        : goal_(goal), graph_(&graph), waits_(goal.kill) {}
    Status act(const World &live, Work &work, unsigned *presses) override;
    std::string why() const override { return why_; }
    std::vector<unsigned> requiresDead() const override { return requiresDead_; }

private:
    bool done(const World &live) const;
    /* For a kill: whether every way near the foe on the MoveGraph passes
     * another foe (on its cell or beside it), and if so the first one on
     * the quickest way. */
    bool blockedBy(const World &w, const Goal &goal, unsigned *foe) const;

    Goal goal_;
    const MoveGraph *graph_;
    std::unique_ptr<Route> route_;
    size_t at_ = 0;
    int    lead_ = 4;      // ticks
    bool   waits_;         // the Route may wait (routing.h)
    int    routes_ = 0;    // Routes pressed to their end without the goal
    std::string why_;      // the last Route's summary
    std::vector<unsigned> requiresDead_;
    bool   approach_ = false;       // the Route walks near the foe, to kill it from there
    bool   approachWaits_ = false;  // and may wait
};

}  // namespace simbot
