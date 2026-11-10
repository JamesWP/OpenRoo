/* GoTo: the Attempt the walking Actions share: a Route to a cell, then its
 * presses, each only while the game is where the Route expected; if it is
 * not, a new Route from where it is, once at rest.
 *
 * A search takes frames, and the game goes on meanwhile, the player standing.
 * Standing goes the same on a copy, so the Route is searched from a copy
 * stood lead ticks ahead, and the player stands until the game gets there;
 * a search that takes longer than its lead leaves the game past its start,
 * and the next has twice the lead.
 *
 * Waiting multiplies a Route's places, so it is searched first without; only
 * if there is no way does it search again with waits (routing.h). */
#pragma once

#include <memory>
#include "actions.h"
#include "movegraph.h"
#include "moves.h"
#include "routing.h"

namespace simbot {

class GoTo : public Attempt {
public:
    GoTo(Cell target, const MoveGraph &graph) : target_(target), graph_(&graph) {}
    Status act(const World &live, Work &work, unsigned *presses) override;

private:
    Cell target_;
    const MoveGraph *graph_;
    std::unique_ptr<Route> route_;
    size_t at_ = 0;
    int    lead_ = 4;   // ticks
    bool   waits_ = false;  // the Route may wait (routing.h)
};

}  // namespace simbot
