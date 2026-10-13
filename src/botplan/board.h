/* The board: the level as the planner (goap.h) plans over it.  The points it
 * can go to, from the model's distances (distances.h): the gems and bomb
 * pickups still on the map, the exit, and the times between them and from
 * the player as it stands.  A point the player cannot reach is left off.
 *
 * Collecting the gem quota is the route optimiser's question (route.h): from
 * a point (or the player), which gems and in what order, on the way to the
 * exit.  The board answers it and keeps the answer for each point. */
#pragma once

#include <vector>
#include "botview.h"
#include "distances.h"
#include "nav.h"

namespace bot {

/* The way to collect the quota from somewhere and go on to the exit. */
struct GemRoute {
    bool              found = false;
    std::vector<int>  gems;        // board points, in visiting order
    double            toLast = 0;  // ms, to the last gem
    double            toExit = 0;  // ms, from the last gem to the exit
};

class Board {
public:
    Board(const BotView &view, const Distances &distances, const Paths &fromPlayer);

    /* Where the player stands, as a point index. */
    static const int PLAYER = -1;

    size_t size() const { return points_.size(); }
    const Point &point(int i) const { return points_[i]; }
    int exit() const { return exit_; }
    const std::vector<int> &gems() const { return gems_; }
    const std::vector<int> &pickups() const { return pickups_; }

    /* The least time from a to b (points, or PLAYER), ms; < 0 if no way. */
    double time(int a, int b) const;

    /* The quota's route from a point (or PLAYER). */
    const GemRoute &gemRoute(int from) const;

    /* What the board starts from: the gems still needed, the bombs held, the
     * foes alive. */
    int gemsNeeded() const { return gemsNeeded_; }
    int bombs() const { return bombs_; }
    int foes() const { return foes_; }

    /* Why there is no way to the goal at all, for the note: "no route: ...". */
    std::string noRoute() const;

private:
    const Distances  *distances_;
    std::vector<int>   index_;        // the distances' point for each board point
    std::vector<Point> points_;
    std::vector<double> fromPlayer_;
    std::vector<int>   gems_, pickups_;
    int exit_ = -1;
    int gemsNeeded_ = 0, bombs_ = 0, foes_ = 0;
    mutable std::vector<GemRoute> routes_;   // by from + 1, filled as asked
    mutable std::vector<bool>     routed_;
};

}  // namespace bot
