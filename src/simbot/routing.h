/* Route: the quickest presses that bring a Goal about, found by playing moves
 * (moves.h) on copies of the World, foes and all: A* on the clock over
 * places, guided by the MoveGraph's time from each place to the goal's
 * cell, a little each frame (work.h).  The first way to a place is the only
 * one kept (of ways as quick, the one with the fewest presses), but for
 * waiting: a place waited on is a node of its own for each wait in a row.  Two waits: a short one, up to shortWaits in a row (for a
 * foe to pass, so only with one near), and till the next of the schedule's
 * wait times (schedule.h), up to timedWaits.  A step from a place where
 * nothing about it runs on the clock (no lift, platform, moving bridge or
 * bomb near, the falling tiles as they were) lands where it did before, as
 * soon: once seen (or as the MoveGraph has it, till a step played lands
 * otherwise), it is not played unless it would come somewhere sooner than
 * known.
 *
 * A goal is never reached with fewer gems held and left than the quota
 * (a blast may take some).
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

class Foe;
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

/* Whether a foe is on the move (it stands still till its search finds a
 * way to the player). */
bool foeMoving(const World &w, const Foe &f);

/* Whether a bomb dropped at place may catch a foe on cell foe at height
 * foeH: where it blows (MoveGraph::blasts, standing or stepping back, as it
 * rolls and falls off ledges) has the foe in its blast, or, the foe moving,
 * is a place it may walk beside (the player's MoveGraph, its time) before
 * it blows. */
bool nearFoe(const MoveGraph &g, const Place &place, Cell foe, int foeH, bool moving);

/* What a Route is for: the player at rest on cell, or, with track, nearFoe
 * foe wherever it has come to (cell is only where it was, to guide the
 * search); or, with kill, foe (a Listed id) killed by a bomb the player drops
 * near it (a DROP, a move or two clear, and the blast waited out: so a foe
 * chasing the player walks into it), the player alive and at rest, the cell
 * then the foe's, to guide the search; or, with trap, foe killed with no
 * bomb, the player alive and at rest and never more than a step or two from
 * cell: the foe led onto a falling tile as it drops. */
struct Goal {
    Cell     cell;
    bool     track = false;
    bool     kill = false;
    bool     trap = false;
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
     * its limits, and for a kill its drops and how they ended. */
    std::string summary() const;

private:
    struct Node {
        int    parent = -1;
        Move   move = FORWARD;
        int    ticks = 0;
        int    stepAt = -1;   // Played::stepAt
        int    waited = 0;    // waits in a row that came to it
        double now = 0;
        double cost = 0;      // what the search counts: now, a step back a little more
        int    presses = 0;   // moves made, waits aside: of ways as quick, the fewest
        double dropAt = -1;   // when the bomb was dropped, if it was
        int    escape = 0;    // moves made since
        Place  place;
        std::unique_ptr<World> world;
        std::tuple<Place, int, int, bool, Cell, Cell> key;   // Key
    };
    bool reached(const Node &n) const;
    /* Whether the gems held and those left (of gemCells_) still make the
     * quota. */
    bool gemsEnough(const World &w) const;
    /* Whether the player stands a while there unharmed (SAFE_MS). */
    bool safe(const Node &n) const;
    bool mayDrop(const Node &n) const;
    /* Whether a foe may come beside the player, standing at n, within its
     * short waits. */
    bool foeNear(const Node &n) const;
    /* Whether a step from place from in w, landing on to, lands so every
     * time it is made with the falling tiles as they are: nothing on the
     * clock near either (a lift, a platform's track, a moving bridge, a
     * bomb), the player carried by nothing. */
    bool still(const World &w, const Place &from, const Place &to) const;
    /* A step from n, as stepsSeen_ holds it: the place, the move, the
     * falling tiles and the way each bridge goes next; false if it is
     * never kept (in the air, after a drop, a trap). */
    using StepKey = std::tuple<Place, int, uint64_t, uint64_t>;
    bool stillKey(const Node &n, int move, StepKey *key) const;
    void found(int at);
    void push(int id);
    /* The place, riding a lift at its height to the nearest whole. */
    Place keyPlace(const Node &n) const;
    /* keyPlace, waited and the way what it rides is going (carried); after a
     * drop, with the foe's cell and the bomb's (the drop's time aside: where
     * they are is what matters); for a trap, the clock and the falling
     * tiles. */
    using Key = std::tuple<Place, int, int, bool, Cell, Cell>;
    Key keyOf(const Node &n) const;

    Goal   goal_;
    const MoveGraph *guide_;
    MoveGraph::Table toCell_;
    size_t maxPlaces_;
    int    shortWaits_;
    int    timedWaits_;
    long   spent_ = 0;    // the work it has taken
    std::unique_ptr<World> start_;
    std::vector<Cell> gemCells_;   // the gems at the start
    std::vector<Node> nodes_;
    using Entry = std::tuple<double, int, int>;   // f, presses, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    std::map<Key, int> best_;
    /* Where a still step lands, and in how long (still). */
    struct Seen {
        Place  to;
        double ms;
    };
    std::map<StepKey, Seen> stepsSeen_;
    /* Where the MoveGraph has a still step land (the falling tiles all
     * up, as it has them), if it has one way for it. */
    bool graphStep(const Node &n, int move, Seen *out) const;
    bool   graphWrong_ = false;   // a step played landed elsewhere than it said
    int    skipped_ = 0;   // steps not played: seen, and no sooner
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
