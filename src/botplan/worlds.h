/* Worlds: the level's terrain (terrain.h) under each set of changes the
 * planner weighs, built when asked for and kept while the terrain stands.
 *
 * A set of changes is a bit per bombable block of the level as it stands (the
 * base, numbered in grid order), set when the block is spent; then a bit per
 * bridge (BotView::bridges), set when it is switched from how it stands; 63
 * bits in all, the blocks first.  The top bit, OPEN, marks the most open
 * world, whose routes pass over switches and glide where glides may be had,
 * any number of times: a bound for the planner, not a world to walk in
 * (Board::everything). */
#pragma once

#include <map>
#include <memory>
#include <stdint.h>
#include "terrain.h"

namespace bot {

using Changes = uint64_t;

const Changes OPEN = (Changes)1 << 63;

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
    /* The world with the changes in s. */
    const Terrain &get(Changes s);
    bool has(Changes s) const { return worlds_.count(s) != 0; }
    size_t size() const { return worlds_.size(); }

    /* The blocks the bits stand for. */
    size_t blockCount() const;
    Step   block(size_t bit) const { return base().blocks()[bit]; }
    /* The bridges that have bits, and the bit of each. */
    size_t bridgeCount() const;
    Changes bridgeBit(size_t i) const { return (Changes)1 << (blockCount() + i); }

private:
    unsigned long long hash_ = 0;
    int facing_ = 1;
    std::map<Changes, std::unique_ptr<Terrain>> worlds_;
};

}  // namespace bot
