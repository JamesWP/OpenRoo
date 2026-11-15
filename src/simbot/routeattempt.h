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
#include "survey.h"

namespace simbot {

class RouteAttempt : public Attempt {
public:
    /* A trap's search takes longer, the foe coming on meanwhile: its lead
     * (ticks) from the first. */
    static const int TRAP_LEAD = 60;
    RouteAttempt(Goal goal, const Survey &survey)
        : goal_(goal), survey_(&survey), graph_(&survey.graph()), lead_(goal.trap ? TRAP_LEAD : 4), waits_(goal.kill || goal.trap) {}
    Status act(const World &live, Work &work, unsigned *presses) override;
    std::string why() const override { return why_; }
    Requirement requirement() const override { return requirement_; }

private:
    bool done(const World &live) const;
    /* For a kill: whether every way on the MoveGraph to where a drop may
     * catch the foe (nearFoe, foeH its height) passes a place another foe
     * comes to first, and if so that foe, the first on the quickest way. */
    bool blockedBy(const World &w, const Goal &goal, int foeH, bool moving, unsigned *foe) const;

    Goal goal_;
    const Survey *survey_;
    const MoveGraph *graph_;
    std::unique_ptr<Route> route_;
    size_t at_ = 0;
    int    lead_;          // ticks
    bool   waits_;         // the Route may wait (routing.h)
    int    routes_ = 0;    // Routes pressed to their end without the goal
    std::string why_;      // the last Route's summary
    Requirement requirement_;
    bool   approach_ = false;       // the Route walks near the foe, to kill it from there
    bool   approachWaits_ = false;  // and may wait
};

}  // namespace simbot
