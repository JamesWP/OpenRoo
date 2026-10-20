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
 * Foes are not seen, but for a cell shut to the routes: a held thrower's,
 * and those aroundThrowers shuts.  A falling tile that does not come back is
 * shut too where crossing it could cut off part of the level (keptOffRuns),
 * unless asked for: crossing it is a step of its own to the planner
 * (board.h, crossings).  Lifts are followed in time
 * (liftmodel.h): a step on or off a lift is allowed only while it is
 * parked, so a move is asked for at a time, and the quickest way waits for
 * the lifts where it must.  A platform
 * is boarded at an end of its track while parked there, and ridden to the
 * other end or stepped off sideways as it passes a cell between (alightings);
 * an extended bridge's deck is ground.
 * tools/bot/modelcheck.py measures how often the model is right, against
 * what the game did (botrun.h's model log). */
#pragma once

#include <map>
#include <set>
#include <vector>
#include "botview.h"
#include "liftmodel.h"

namespace bot {

/* The player's step time under the speed effects (player.cpp), and when
 * each runs out (ms from the view's clock; never if huge): on the first tick
 * past its end, and either running out sets the step time back to 200 ms. */
struct Speed {
    double stepMs   = 200;
    double upEnds   = 1e300;
    double downEnds = 1e300;
};

struct Outcome {
    enum Kind { Blocked, Rest, Death };
    Kind   kind = Blocked;
    int    u = 0, v = 0, facing = 0;  // where it comes to rest
    double ms = 0;                    // until the next move can start: the landing of a drop in it
    std::vector<Step> cells;          // the cells it passes, the last where it rests
    std::vector<double> at;           // when the step into each of them starts, ms from the move's
    const char *why = "";             // what blocked or killed it
    Speed speed;                      // the step time after it
};

class NavModel {
public:
    /* With oneShots, the routes may cross the falling tiles that do not
     * come back; else they keep off the runs of them that bridge
     * (keptOffRuns). */
    explicit NavModel(const BotView &view, bool oneShots = false);

    /* From rest at (u, v) facing f: a step in direction d (f is forward, the
     * opposite of f is back) or a turn (delta +1 right, -1 left). */
    Outcome step(int u, int v, int f, int d) const;
    /* The same, started atMs from the view's clock: a step on or off a lift
     * is blocked unless the lift is parked through it.  Under speed, or the
     * view's if null: each step's time, tick by tick, as the speed effects
     * have it, with those it picks up. */
    Outcome stepAt(int u, int v, int f, int d, double atMs, const Speed *speed = nullptr) const;
    /* The earliest start at or after atMs of a step that is not blocked, and
     * its outcome; Blocked if none (a lift never parks, or not within the
     * model's horizon).  *startMs is when it starts. */
    Outcome stepWhenOpen(int u, int v, int f, int d, double atMs, double *startMs,
                         const Speed *speed = nullptr) const;
    /* Whether anything on the level changes with the clock that the routes
     * must wait for or time: a lift, a platform, a fallen tile coming back. */
    bool timed() const { return !spells_.empty() || !back_.empty(); }
    /* Whether the step time changes with the clock: a speed effect runs, or
     * one is to be picked up. */
    bool speedy() const { return speedy_; }
    /* The step time as the view has it. */
    const Speed &speed() const { return speed_; }
    /* Riding a platform: the player who stands on the end cell (u, v) at atMs
     * is carried to the other end and can step off from the next park there.
     * False if no platform ends on the cell; *pu, *pv the other end's cell
     * and *arriveMs when it parks there. */
    bool ride(int u, int v, double atMs, int *pu, int *pv, double *arriveMs) const;
    /* Where else the player on the end cell (u, v) at atMs can step off the
     * platform: each cell strictly between its ends, with the first moment
     * after atMs the platform stands over it (platformPasses). */
    struct Alighting {
        Step   at;
        double ms;
    };
    std::vector<Alighting> alightings(int u, int v, double atMs) const;
    /* A step in direction d, across the track, off the platform standing over
     * (u, v) at atMs, at the platform's height. */
    Outcome stepOff(int u, int v, int f, int d, double atMs, const Speed *speed = nullptr) const;
    /* Whether the cell is one a lift or a platform carries: open only while
     * it is parked there. */
    static bool moving(const Cell &c);
    /* The time, after the step, until the next move can start, when the
     * step drops that many heights; 0 for no fall.  By the game's free fall
     * (movableentity.cpp), tick by tick: the height falls 3 t + 5.405 t^2
     * in t s, the fall ends on the tick after it reaches the floor, and a
     * key pressed then moves the player on the tick after that (26 ticks
     * for a drop of two).  Onto stairs, the fall ends half a height above
     * them.  A move's time (step, stepAt) has it in. */
    static double fallMs(int drop, double tickMs, bool stairs = false);
    /* How long after the step onto it a sticky pad lets the player move:
     * the first tick more than 3 s after the one it caught it on
     * (movableentity.cpp). */
    static double stickyMs(double tickMs);
    /* How long after the step onto it a jump pad that rises this many
     * heights sets the player off on the next step: the launch climbs
     * v0 t - 4.905 t^2, v0 from the rise, and on the tick after it is at the
     * top the player is there and steps on (movableentity.cpp). */
    static double jumpMs(int rise, double tickMs);
    Outcome turn(int u, int v, int f, int delta, double atMs = 0,
                 const Speed *speed = nullptr) const;
    /* The falling tile on c is armed atMs from the view's clock, by a walk
     * planned to go over it: it is gone while it is fallen (fallingmodel.h).
     * The model the routes after that walk are found in. */
    void arm(Step c, double atMs);
    /* No step goes into c (aroundThrowers). */
    void shut(Step c) { shut_.insert(c.v * view_->extentU + c.u); }
    bool anyShut() const { return !shut_.empty(); }

    static int opposite(int d) { return d > 2 ? d - 2 : d + 2; }
    static int turned(int d, int delta) { return (d - 1 + delta + 4) % 4 + 1; }

    const BotView &view() const { return *view_; }

private:
    /* Whether the game refuses a step from a (standing at height h) to b in
     * direction d, and why. */
    const char *refused(const Cell &a, int h, const Cell &b, int d) const;
    /* From a platform that carries the player when fromHeight >= 0. */
    Outcome stepImpl(int u, int v, int f, int d, bool timed, double atMs, const Speed *speed,
                     int fromHeight = -1) const;
    /* A move started a ms from the view's clock under *speed, taking
     * fixedMs (a slide's) or else the step time: until the next move can
     * start, ms from a, and *speed after it, with the pickup on the cell it
     * goes to (TileContents, 0 for none) taken on the way. */
    double timeMove(double a, int pickup, double fixedMs, Speed *speed) const;
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
        double from = -1e300;   // when it falls, for one not fallen yet (arm)
    };
    std::map<int, Fallen> back_;                // a fallen or armed tile's cell index
    std::map<int, int> partner_;                // a platform's end cell index to its other end's
    std::map<int, std::vector<Alighting>> alight_;  // a platform's end cell index to its passes
    std::set<int> shut_;                        // cell indices shut()
    std::set<int> oneShot_;                     // the one-shot tiles kept off
    Speed speed_;                               // the view's
    bool  speedy_ = false;
};

/* The bridge the switch on (u, v) works, or null if it is no working switch. */
const BridgeView *switchedBy(const BotView &view, int u, int v);

/* Whether a held thrower stands on (u, v), at rest: a follower with no
 * catcher left to follow stays there, in the way (foetype.h). */
bool parkedFoe(const BotView &view, int u, int v);

/* The runs of one-shot falling tiles (fallingmodel.h, oneShotRuns) the
 * walks keep off: those that bridge, without which the ground beside the run
 * cannot all reach itself, so that crossing one can cut off part of the
 * level.  Per cell as oneShotRuns, -1 for a run that bridges nothing. */
std::vector<int> keptOffRuns(const BotView &view);

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
    bool              stepOff = false;  // cells[0] is a platform passing at waitUntil:
                                        // the step to cells[1] is taken off it then,
    int               facing = 0;       // facing this way, turned to while on board
                                        // or before the wait
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

    /* When the routes set off, ms from the view's clock. */
    double startMs() const { return startMs_; }

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
    /* The step off a platform into state to (how_ 6 or 7) from state from. */
    Outcome alighted(int from, int to) const;
    int index(int u, int v, int f) const { return ((v * eu_) + u) * 4 + (f - 1); }

    const NavModel &nav_;
    int eu_, ev_;
    int startU_, startV_;
    double startMs_;
    std::vector<double> cost_;
    std::vector<int>    parent_;     // state index, -1 at the start
    std::vector<signed char> how_;   // the move into it: 0 forward, 1 back, 2 right, 3 left,
                                     // 4 a ride, 5 a glide, 6 forward or 7 back off a platform
    std::vector<int>    alightAt_;   // for 6 and 7: the cell index stepped off from
    std::vector<double> departs_;    // when the move into it started
    std::vector<Speed>  speed_;      // the step time on coming to rest in it
};

/* The nav model with the throwers' cells shut, the one each is in and,
 * mid-step, the one it is leaving: the player cannot walk through one
 * (movableentity.cpp).  Only of those the player, setting off startMs from
 * the view's clock, could come to within withinMs: a thrower further off
 * moves on before the player gets there. */
NavModel aroundThrowers(const BotView &view, double startMs, double withinMs);

}  // namespace bot
