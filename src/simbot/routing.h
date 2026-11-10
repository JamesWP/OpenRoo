/* Route: the quickest presses that bring the player to rest on a cell, found
 * by playing moves (moves.h) on copies of the World, foes and all: Dijkstra
 * on the clock over places, a little each frame (work.h): A*, guided by the
 * MoveGraph's time from each place to the target.  The first way to
 * a place is the only one kept, but for waiting: a place waited on is a
 * node of its own for each wait in a row.  Two waits: a short one, up to
 * shortWaits in a row (for a foe to pass), and till the next of the schedule's wait times (schedule.h), up to timedWaits.
 *
 * Found, it holds the presses of each tick and the World expected before
 * each (Expect), so the one pressing them can tell when the game goes
 * otherwise. */
#pragma once

#include <map>
#include <memory>
#include <queue>
#include <vector>
#include "movegraph.h"
#include "moves.h"

class World;

namespace simbot {

/* What must match before each press: the clock, and the player's cell,
 * height, facing and move under way. */
struct Expect {
    double now;
    int    u, v, h, facing, moveDir;
    bool operator==(const Expect &o) const;
};
Expect expectOf(const World &w);

class Route {
public:
    Route(const World &start, Cell target, const MoveGraph &guide, size_t maxPlaces = 6000,
          int shortWaits = 0, int timedWaits = 0);
    ~Route();

    enum Status { Searching, Found, NoWay };
    Status run(Work &work);

    const std::vector<unsigned> &presses() const { return presses_; }
    const std::vector<Expect>   &expects() const { return expects_; }
    /* The places played from so far. */
    size_t places() const { return nodes_.size(); }

private:
    struct Node {
        int    parent = -1;
        Move   move = FORWARD;
        int    ticks = 0;
        int    waited = 0;   // waits in a row that came to it
        double now = 0;
        Place  place;
        std::unique_ptr<World> world;
    };
    void found(int at);
    void push(int id);

    Cell   target_;
    const MoveGraph *guide_;
    const std::vector<double> *toTarget_;
    size_t maxPlaces_;
    int    shortWaits_;
    int    timedWaits_;
    std::unique_ptr<World> start_;
    std::vector<Node> nodes_;
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<std::pair<Place, int>, int> best_;   // by place and waited
    int    expanding_ = -1;   // the node whose moves are being played
    int    move_ = 0;         // its next move
    Status status_ = Searching;
    std::vector<unsigned> presses_;
    std::vector<Expect>   expects_;
};

}  // namespace simbot
