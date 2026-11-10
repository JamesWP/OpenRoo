/* GoTo: the Attempt the walking Actions share: a Route to a cell, then its
 * presses, each only while the game is where the Route expected; if it is
 * not, a new Route from where it is, once at rest.
 *
 * A search takes frames, and the game goes on meanwhile, the player standing.
 * Standing goes the same on a copy, so the Route is searched from a copy
 * stood lead ticks ahead, and the player stands until the game gets there;
 * a search that takes longer than its lead leaves the game past its start,
 * and the next has twice the lead. */
#pragma once

#include <memory>
#include "actions.h"
#include "moves.h"
#include "routing.h"

namespace simbot {

class GoTo : public Attempt {
public:
    explicit GoTo(Cell target) : target_(target) {}
    Status act(const World &live, Work &work, unsigned *presses) override;

private:
    Cell target_;
    std::unique_ptr<Route> route_;
    size_t at_ = 0;
    int    lead_ = 4;   // ticks
};

}  // namespace simbot
