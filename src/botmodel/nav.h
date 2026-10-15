/* The nav model: where a move takes the player, by the rules of the terrain,
 * and the quickest ways around the level that follow from them.
 *
 * A move starts from rest on a cell, facing one way: a turn, or a step
 * forward or back (back steps without turning).  The model follows the step
 * and whatever it forces after it (a slide carries on along its direction, ice
 * the way the step went, a teleporter moves the player and then on one step, a
 * jump pad launches it on, a sticky pad holds it) until the player is at rest
 * again, or dead (stepped into the void, dropped too far, walked at a face
 * two or more high, which the game lets it do and drops it down, or stepped
 * across into the cell of a raised stair, under the block and into the open
 * space below it), or the step
 * is refused (a wall, a climb of one without stairs).
 *
 * A switch is a route's end, never a way through: the player who comes to
 * rest on one works its bridge (switchedBy), which only a plan that means to
 * may do.
 *
 * Foes are not seen.  Lifts are followed in time (liftmodel.h): a step on or
 * off a lift is allowed only while it is parked, so a move is asked for at a
 * time, and the quickest way waits for the lifts where it must.  Platforms
 * are taken as walls; an extended bridge's deck is ground.
 * tools/bot/modelcheck.py measures how often the model is right, against
 * what the game did (botrun.h's model log). */
#pragma once

#include <map>
#include <vector>
#include "botview.h"
#include "liftmodel.h"

namespace bot {

struct Outcome {
    enum Kind { Blocked, Rest, Death };
    Kind   kind = Blocked;
    int    u = 0, v = 0, facing = 0;  // where it comes to rest
    double ms = 0;                    // until then
    std::vector<Step> cells;          // the cells it passes, the last where it rests
    const char *why = "";             // what blocked or killed it
};

class NavModel {
public:
    explicit NavModel(const BotView &view);

    /* From rest at (u, v) facing f: a step in direction d (f is forward, the
     * opposite of f is back) or a turn (delta +1 right, -1 left). */
    Outcome step(int u, int v, int f, int d) const;
    /* The same, started atMs from the view's clock: a step on or off a lift
     * is blocked unless the lift is parked through it. */
    Outcome stepAt(int u, int v, int f, int d, double atMs) const;
    /* The earliest start at or after atMs of a step that is not blocked, and
     * its outcome; Blocked if none (a lift never parks, or not within the
     * model's horizon).  *startMs is when it starts. */
    Outcome stepWhenOpen(int u, int v, int f, int d, double atMs, double *startMs) const;
    /* Whether anything on the level changes with the clock that the routes
     * must wait for or time: a lift, a platform, a fallen tile coming back. */
    bool timed() const { return !spells_.empty() || !back_.empty(); }
    /* Riding a platform: the player who stands on the end cell (u, v) at atMs
     * is carried to the other end and can step off from the next park there.
     * False if no platform ends on the cell; *pu, *pv the other end's cell
     * and *arriveMs when it parks there. */
    bool ride(int u, int v, double atMs, int *pu, int *pv, double *arriveMs) const;
    /* Whether the cell is one a lift or a platform carries: open only while
     * it is parked there. */
    static bool moving(const Cell &c);
    /* The time, after the step, to land a drop of that many heights (measured,
     * tools/bot/calibrate.py: 283 ms for one or two, 433 for three); 0 for no
     * fall.  A move's time (step, step) leaves it out: the routes of a whole
     * level are estimates and the drops do not change which is quickest by much;
     * the timing of a script (a bomb's) adds it, fallsAlong. */
    static double fallMs(int drop);
    /* The time the drops along a walk of these cells take to land. */
    double fallsAlong(const std::vector<Step> &cells) const;
    Outcome turn(int u, int v, int f, int delta) const;

    static int opposite(int d) { return d > 2 ? d - 2 : d + 2; }
    static int turned(int d, int delta) { return (d - 1 + delta + 4) % 4 + 1; }

    const BotView &view() const { return *view_; }

private:
    /* Whether the game refuses a step from a (standing at height h) to b in
     * direction d, and why. */
    const char *refused(const Cell &a, int h, const Cell &b, int d) const;
    Outcome stepImpl(int u, int v, int f, int d, bool timed, double atMs) const;
    /* The height the lift on (u, v) stands parked at through [atMs, atMs +
     * forMs], or -1. */
    int liftHeight(int u, int v, double atMs, double forMs) const;
    /* The ms at which the lift on (u, v) next starts a park after atMs. */
    void parkStarts(int u, int v, double atMs, std::vector<double> *out) const;

    const BotView *view_;
    std::map<int, std::vector<Park>> spells_;   // by cell index, for each lift and platform end
    struct Fallen {
        double back;   // when it is back, ms from the view's clock
        int    height;
    };
    std::map<int, Fallen> back_;                // a fallen tile's cell index
    std::map<int, int> partner_;                // a platform's end cell index to its other end's
};

/* The bridge the switch on (u, v) works, or null if it is no working switch. */
const BridgeView *switchedBy(const BotView &view, int u, int v);

/* A hash of what the nav model reads (the terrain and the step time): its
 * answers stand while this does. */
unsigned long long navHash(const BotView &view);

/* A moment of a timed plan: from t ms on, the player is on cell (its cell
 * changes when a step starts, as the game's does). */
struct Timed {
    double t;
    Step   cell;
    bool   aloft = false;  // high above the cell (gliding): out of reach of what is on it
};

/* Where a glide comes down (glide.h). */
struct Landing {
    Step   cell;
    int    facing = 0;          // the player's, as it lands
    double ms = 0;              // from the launch, at rest, to landing
    std::vector<Step>  path;    // the launch cell, then the cells the player enters
    std::vector<Timed> schedule;  // the player's cell over time, ms from the launch
};

/* The landings of glides, by launch (Terrain keeps them). */
class GlideSource {
public:
    virtual ~GlideSource() = default;
    virtual const std::vector<Landing> &glides(int u, int v, int facing) const = 0;
};

/* A stretch of a route: cells entered one move after another, after waiting
 * until waitUntil (ms from the view's clock; 0 for no wait). */
struct Leg {
    double            waitUntil = 0;
    std::vector<Step> cells;
};

/* The quickest routes from one rest state to every other: Dijkstra over
 * (cell, facing), each move costing its time, and, where lifts are, waiting
 * for them (the routes start at the view's clock).  A route ends at a working
 * switch, unless it starts there. */
class Paths {
public:
    /* From rest on (u, v) facing f, setting off startMs from the view's
     * clock (it matters where there are lifts).  With throughSwitches, the
     * routes pass over switches as over ground, and with glides they glide
     * where they can, as often as they like: bounds, for the planner's most
     * open world (worlds.h), not ways to be walked. */
    Paths(const NavModel &nav, int u, int v, int facing, double startMs = 0,
          bool throughSwitches = false, const GlideSource *glides = nullptr);

    /* The least time from the start to come to rest on (u, v), any facing;
     * < 0 if never. */
    double cost(int u, int v) const;

    /* The facing the quickest route to (u, v) arrives with; 0 if never. */
    int facingAt(int u, int v) const;

    /* The cells to walk to (u, v) by the quickest route, starting with the
     * start cell, as the motor follows them; empty if unreachable.  Waits
     * for lifts are not in it: see legsTo. */
    std::vector<Step> cellsTo(int u, int v) const;

    /* The route as legs, each walked after the wait before it (waitUntil is
     * ms from the view's clock); the first cell of the first leg is the
     * start. */
    std::vector<Leg> legsTo(int u, int v) const;

    /* The same route as a schedule: the start cell at 0, then each cell the
     * route enters at the time it is entered (from the start), waits
     * included. */
    std::vector<Timed> scheduleTo(int u, int v) const;

private:
    int bestState(int u, int v) const;
    std::vector<int> chainTo(int u, int v) const;
    int index(int u, int v, int f) const { return ((v * eu_) + u) * 4 + (f - 1); }

    const NavModel &nav_;
    int eu_, ev_;
    int startU_, startV_;
    double startMs_;
    std::vector<double> cost_;
    std::vector<int>    parent_;     // state index, -1 at the start
    std::vector<signed char> how_;   // the move into it: 0 forward, 1 back, 2 right, 3 left
    std::vector<double> departs_;    // when the move into it started
};

}  // namespace bot
