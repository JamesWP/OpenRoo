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
 * (a blast may take some).  A bomb dropped (as the Goal allows) is got
 * clear of, a step or two, and its blast waited out.
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

/* From a drop till its blast is gone, and a tick. */
extern const double BLAST_OVER_MS;

/* Whether the player stands a while in w unharmed (SAFE_MS): a foe may be
 * on its way. */
bool safe(const World &w);

class Goal;

class Route {
public:
    Route(const World &start, std::shared_ptr<Goal> goal, const MoveGraph &guide,
          size_t maxPlaces = 6000,
          int shortWaits = 0, int timedWaits = 0);
    ~Route();

    enum Status { Searching, Found, NoWay };
    Status run(Work &work);

    const std::vector<unsigned> &presses() const { return presses_; }
    const std::vector<Expect>   &expects() const { return expects_; }
    /* The places played from so far. */
    size_t places() const { return places_; }
    /* What the search came to, for the log: its places, whether it hit
     * its limits, its drops and how they ended, and the goal's say. */
    std::string summary() const;

    /* A node of the search: a place come to, and the World there. */
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
    /* What a node is told apart by: keyPlace, waited and the way what it
     * rides is going (carried); after a drop (true), the cells of what the
     * goal follows and of the bomb (the drop's time aside: where they are
     * is what matters); a Goal may make its own. */
    using Key = std::tuple<Place, int, int, bool, Cell, Cell>;

    bool reached(const Node &n) const;
    /* Whether the gems held and those left (of gemCells_) still make the
     * quota. */
    bool gemsEnough(const World &w) const;
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
     * never kept (in the air, after a drop). */
    using StepKey = std::tuple<Place, int, uint64_t, uint64_t>;
    bool stillKey(const Node &n, int move, StepKey *key) const;
    void found(int at);
    void push(int id);
    /* The place, riding a lift at its height to the nearest whole. */
    Place keyPlace(const Node &n) const;
    Key keyOf(const Node &n) const;

    std::shared_ptr<Goal> goal_;
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
    int    drops_ = 0, diedAfterDrop_ = 0;
    std::vector<unsigned> presses_;
    std::vector<Expect>   expects_;
};

/* What a Route searches for.  This one is the plain goal: the player at
 * rest on cell; a kill's and a trap's are in kill.h and trap.cpp. */
class Goal {
public:
    explicit Goal(Cell cell) : cell(cell) {}
    virtual ~Goal() = default;
    Cell cell;   // guides the search: the MoveGraph's time to it
    /* Whether the search has it at n (the quota aside: the Route sees to it). */
    virtual bool reached(const Route::Node &n) const;
    /* The ms left from n, toCell the MoveGraph's time from n to cell. */
    virtual double estimate(const Route::Node &, double toCell) const { return toCell; }
    /* n's Key, plain the Route's own. */
    virtual Route::Key key(const Route::Node &, const Route::Key &plain) const { return plain; }
    /* Whether a bomb may be dropped at n (the Route sees one is held). */
    virtual bool mayDrop(const Route::Node &) const { return false; }
    /* Whether n, just played, is searched on. */
    virtual bool keeps(const Route::Node &) { return true; }
    /* Its part of the Route's summary, for the log. */
    virtual std::string summary() const { return ""; }
    /* Whether the live game has it (RouteAttempt). */
    virtual bool done(const World &live) const;
};

}  // namespace simbot
