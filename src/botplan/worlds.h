/* Worlds: the level's terrain (terrain.h) under each set of spent blocks the
 * planner weighs, built when asked for and kept while the terrain stands.
 *
 * A set is a bit per bombable block of the level as it stands (the base,
 * numbered in grid order), the first 64 blocks. */
#pragma once

#include <map>
#include <memory>
#include <stdint.h>
#include "terrain.h"

namespace bot {

using Spent = uint64_t;

class Worlds {
public:
    /* Makes the view's terrain the base, forgetting the worlds built on
     * another one. */
    void update(const BotView &view);

    const Terrain &base() const { return *worlds_.at(0); }
    /* The facing the routes from the points of interest are found for: the
     * player's when the base was made.  It is a turn or two off the facing it
     * will really have, and the same for every decision on the same terrain,
     * so the routes are found once. */
    int pointFacing() const { return facing_; }
    /* The world with the blocks in s spent. */
    const Terrain &get(Spent s);
    bool has(Spent s) const { return worlds_.count(s) != 0; }
    size_t size() const { return worlds_.size(); }

    /* The blocks the bits stand for. */
    size_t blockCount() const;
    Step   block(size_t bit) const { return base().blocks()[bit]; }

private:
    unsigned long long hash_ = 0;
    int facing_ = 1;
    std::map<Spent, std::unique_ptr<Terrain>> worlds_;
};

}  // namespace bot
