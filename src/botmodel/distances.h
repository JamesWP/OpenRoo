/* Distances: the level's points of interest, and the quickest times between
 * them (nav.h), kept while the terrain stands.
 *
 * The points are the gems, the bomb pickups and the exit, in that order, each
 * kind in grid order (v, then u).  They are found when the terrain changes
 * and kept as they were: a point whose pickup has since been taken stays,
 * and the caller checks the cell.  A time from a point is from rest on it
 * facing the way the player faces now, a turn or two off the facing it will
 * really have at most. */
#pragma once

#include <vector>
#include "botview.h"

namespace bot {

struct Point {
    enum Kind { Gem, Bombs, Exit };
    Kind kind;
    Step cell;
};

class Distances {
public:
    /* Surveys again if the terrain has changed since the last survey. */
    void update(const BotView &view);

    const std::vector<Point> &points() const { return points_; }
    /* The index of the point on c, or -1. */
    int find(Step c) const;
    /* The least time from rest on point i to rest on point j, ms; < 0 if
     * there is no way. */
    double between(size_t i, size_t j) const { return between_[i][j]; }

private:
    unsigned long long terrain_ = 0;
    bool               done_ = false;
    std::vector<Point> points_;
    std::vector<std::vector<double>> between_;
};

}  // namespace bot
