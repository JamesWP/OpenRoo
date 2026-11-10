/* The survey: what the bot works out once, at the start of a level, with a
 * budget of its own (the frames' budgets are for the rest).  The MoveGraph,
 * the gems and the exit; more as they are needed. */
#pragma once

#include <vector>
#include "movegraph.h"

class World;

namespace simbot {

class Survey {
public:
    explicit Survey(const World &start);

    const MoveGraph &graph() const { return graph_; }
    /* The cells the level's gems lie on at the start, in grid order: a gem
     * is known by its index here (the first GemSet::MAX). */
    const std::vector<Cell> &gems() const { return gems_; }
    Cell exit() const { return exit_; }
    int  quota() const { return quota_; }

private:
    MoveGraph         graph_;
    std::vector<Cell> gems_;
    Cell              exit_;
    int               quota_ = 0;
};

}  // namespace simbot
