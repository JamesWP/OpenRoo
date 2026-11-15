/* The kill's half of the router: its Goals, and what they and the kill's
 * RouteAttempt work out about a foe (kill.cpp). */
#pragma once

#include "movegraph.h"
#include "routing.h"

class Foe;
class World;

namespace simbot {

/* Whether a foe is on the move (it stands still till its search finds a
 * way to the player). */
bool foeMoving(const World &w, const Foe &f);

/* Whether a bomb dropped at place may catch a foe on cell foe at height
 * foeH: where it blows (MoveGraph::blasts, standing or stepping back, as it
 * rolls and falls off ledges) has the foe in its blast, or, the foe moving,
 * is a place it may walk beside (the player's MoveGraph, its time) before
 * it blows. */
bool nearFoe(const MoveGraph &g, const Place &place, Cell foe, int foeH, bool moving);

/* Whether every way on the MoveGraph from the player in w to where a drop
 * may catch foe (on cell, at height foeH: nearFoe) passes a place another
 * foe comes to first, and if so that foe, the first on the quickest way. */
bool blockedBy(const MoveGraph &g, const World &w, unsigned foe, Cell cell, int foeH,
               bool moving, unsigned *blocker);

/* foe (a Listed id) killed by a bomb the player drops near it (a DROP, a
 * move or two clear, and the blast waited out: so a foe chasing the player
 * walks into it), every bomb gone and the player alive and at rest; cell is
 * the foe's, to guide the search. */
class KillGoal : public Goal {
public:
    KillGoal(Cell cell, unsigned foe, const MoveGraph &graph) : Goal(cell), foe_(foe), graph_(&graph) {}
    unsigned foe() const { return foe_; }
    bool reached(const Route::Node &n) const override;
    double estimate(const Route::Node &n, double toCell) const override;
    Route::Key key(const Route::Node &n, const Route::Key &plain) const override;
    bool mayDrop(const Route::Node &n) const override;
    bool keeps(const Route::Node &n) override;
    std::string summary() const override;
    bool done(const World &live) const override;

private:
    unsigned foe_;
    const MoveGraph *graph_;
    int lived_ = 0;   // drops whose blast was over with the foe alive
};

/* The player nearFoe foe wherever it has come to (cell is only where it
 * was, to guide the search), alive and at rest; or the foe gone. */
class TrackGoal : public Goal {
public:
    TrackGoal(Cell cell, unsigned foe, const MoveGraph &graph) : Goal(cell), foe_(foe), graph_(&graph) {}
    bool reached(const Route::Node &n) const override;

private:
    unsigned foe_;
    const MoveGraph *graph_;
};

}  // namespace simbot
