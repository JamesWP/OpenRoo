/* Route: the quickest presses that bring a Goal about, found by playing moves
 * (moves.h) on copies of the World, foes and all: A* on the clock over
 * places, guided by the MoveGraph's time from each place to the goal's
 * cell, a little each frame (work.h).  The first way to a place is the only
 * one kept, but for waiting: a place waited on is a node of its own for each
 * wait in a row.  Two waits: a short one, up to shortWaits in a row (for a
 * foe to pass), and till the next of the schedule's wait times
 * (schedule.h), up to timedWaits.
 *
 * Found, it holds the presses of each tick and the World expected before
 * each (Expect), so the one pressing them can tell when the game goes
 * otherwise. */
#pragma once

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <tuple>
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

/* What a Route is for: the player at rest on cell, or within near steps of
 * it; or, with kill, foe (a Listed id) killed by a bomb the player drops
 * near it (a DROP, a move or two clear, and the blast waited out: so a foe
 * chasing the player walks into it), the player alive and at rest, the cell
 * then the foe's, to guide the search. */
struct Goal {
    Cell     cell;
    int      near = 0;
    bool     kill = false;
    unsigned foe = 0;
};

class Route {
public:
    Route(const World &start, Goal goal, const MoveGraph &guide, size_t maxPlaces = 6000,
          int shortWaits = 0, int timedWaits = 0);
    ~Route();

    enum Status { Searching, Found, NoWay };
    Status run(Work &work);

    const std::vector<unsigned> &presses() const { return presses_; }
    const std::vector<Expect>   &expects() const { return expects_; }
    /* The places played from so far. */
    size_t places() const { return places_; }
    /* What the search came to, for the log: its places, whether it hit
     * maxPlaces, and for a kill its drops and how they ended. */
    std::string summary() const;

private:
    struct Node {
        int    parent = -1;
        Move   move = FORWARD;
        int    ticks = 0;
        int    waited = 0;    // waits in a row that came to it
        double now = 0;
        double dropAt = -1;   // when the bomb was dropped, if it was
        int    escape = 0;    // moves made since
        Place  place;
        std::unique_ptr<World> world;
    };
    bool reached(const Node &n) const;
    bool mayDrop(const Node &n) const;
    void found(int at);
    void push(int id);
    using Key = std::tuple<Place, int, bool>;   // place, waited, dropped
    static Key keyOf(const Node &n) { return {n.place, n.waited, n.dropAt >= 0}; }

    Goal   goal_;
    const MoveGraph *guide_;
    const std::vector<double> *toCell_;
    size_t maxPlaces_;
    int    shortWaits_;
    int    timedWaits_;
    std::unique_ptr<World> start_;
    std::vector<Node> nodes_;
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<Key, int> best_;
    int    expanding_ = -1;   // the node whose moves are being played
    int    move_ = 0;         // its next move
    Status status_ = Searching;
    size_t places_ = 1;
    bool   capped_ = false;
    int    drops_ = 0, foeLived_ = 0, diedAfterDrop_ = 0;
    std::vector<unsigned> presses_;
    std::vector<Expect>   expects_;
};

}  // namespace simbot
