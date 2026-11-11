/* The survey: what the bot works out once, at the start of a level, with a
 * budget of its own (the frames' budgets are for the rest).  The MoveGraph,
 * the gems and the exit; more as they are needed. */
#pragma once

#include <map>
#include <vector>
#include "movegraph.h"

class World;

namespace simbot {

class Survey {
public:
    explicit Survey(const World &start);

    const MoveGraph &graph() const { return graph_; }
    /* The cells the level's gems lie on at the start, in grid order: a gem
     * is known by its index here (the first IndexSet::MAX). */
    const std::vector<Cell> &gems() const { return gems_; }
    /* The cells of the bomb pickups (3 bombs each) at the start, known by
     * their index like the gems. */
    const std::vector<Cell> &pickups() const { return pickups_; }
    /* The cells of the gliders (a glider charge each), likewise. */
    const std::vector<Cell> &gliders() const { return gliders_; }
    /* The foes a bomb kills (the catchers; throwers are proof against
     * blasts) at the start, by Listed id, and the cell each was last seen
     * on (see), known by their index here (the first 64). */
    const std::vector<unsigned> &foes() const { return foes_; }
    Cell seen(int i) const { return seen_[i]; }
    /* The cells of the falling tiles: a foe stood on one when it drops
     * falls with it (trap.cpp). */
    const std::vector<Cell> &fallings() const { return fallings_; }
    /* Where a foe on cell foe may be trapped (trap.cpp): a falling tile it
     * comes to, and a stand beside it, not a falling tile, that it comes to
     * only over falling tiles, by way of that one; with the foe's times to
     * the tile and the stand on the player's MoveGraph.  Its tiles soonest
     * first.
     * Worked out for each cell the first time it is asked for. */
    struct Stand {
        Cell   tile, stand;
        double tileMs, standMs;
    };
    const std::vector<Stand> &stands(Cell foe) const;
    /* Notes where the foes are now. */
    void see(const World &w);

    Cell exit() const { return exit_; }
    int  quota() const { return quota_; }

private:
    MoveGraph         graph_;
    std::vector<Cell> gems_;
    std::vector<Cell> pickups_;
    std::vector<Cell> gliders_;
    std::vector<unsigned> foes_;
    std::vector<Cell> seen_;
    std::vector<Cell> fallings_;
    mutable std::map<Cell, std::vector<Stand>> stands_;
    Cell              exit_;
    int               quota_ = 0;
};

}  // namespace simbot
