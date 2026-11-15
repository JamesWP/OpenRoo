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
 * only if there is no way does it search again with waits; a goal that
 * needs them (a foe to come on) waits from the first (RouteOptions). */
#pragma once

#include <memory>
#include "actions.h"
#include "movegraph.h"
#include "moves.h"
#include "routing.h"
#include "survey.h"

namespace simbot {

/* How a RouteAttempt searches: its Routes' places at most, waits in a row
 * (short ones, with foes about, and timed: routing.h), whether it waits from
 * the first, and its first lead (ticks). */
struct RouteOptions {
    size_t places = 6000;
    /* Waits in a row for a foe to pass: 2 s. */
    int    shortWaits = 8;
    /* Timed waits in a row (schedule.h): with a lift or a platform passing
     * each height or cell, each way, a cycle or so. */
    int    timedWaits = 24;
    bool   waitFirst = false;
    int    lead = 4;
};

class RouteAttempt : public Attempt {
public:
    RouteAttempt(std::shared_ptr<Goal> goal, const Survey &survey, RouteOptions options = {})
        : goal_(std::move(goal)), survey_(&survey), graph_(&survey.graph()), options_(options),
          lead_(options.lead), waits_(options.waitFirst) {}
    Status act(const World &live, Work &work, unsigned *presses) override;
    std::string why() const override { return why_; }
    Requirement requirement() const override { return requirement_; }

private:
    std::shared_ptr<Goal> goal_;
    const Survey *survey_;
    const MoveGraph *graph_;
    RouteOptions options_;
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
