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
 * onto stairs across them from no higher, which drops it through), or the step
 * is refused (a wall, a climb of one without stairs).
 *
 * Static terrain only, for now: foes are not seen, and lifts, platforms and
 * bridges are taken as walls.  tools/bot/modelcheck.py measures how often the
 * model is right, against what the game did (botrun.h's model log). */
#pragma once

#include <vector>
#include "botview.h"
#include "motor.h"

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
    explicit NavModel(const BotView &view) : view_(&view) {}

    /* From rest at (u, v) facing f: a step in direction d (f is forward, the
     * opposite of f is back) or a turn (delta +1 right, -1 left). */
    Outcome step(int u, int v, int f, int d) const;
    Outcome turn(int u, int v, int f, int delta) const;

    static int opposite(int d) { return d > 2 ? d - 2 : d + 2; }
    static int turned(int d, int delta) { return (d - 1 + delta + 4) % 4 + 1; }

    const BotView &view() const { return *view_; }

private:
    /* Whether the game refuses a step from a (standing at height h) to b in
     * direction d, and why. */
    const char *refused(const Cell &a, int h, const Cell &b, int d) const;

    const BotView *view_;
};

/* The quickest routes from one rest state to every other: Dijkstra over
 * (cell, facing), each move costing its time. */
class Paths {
public:
    Paths(const NavModel &nav, int u, int v, int facing);

    /* The least time to come to rest on (u, v), any facing; < 0 if never. */
    double cost(int u, int v) const;

    /* The cells to walk to (u, v) by the quickest route, starting with the
     * start cell, as the motor follows them; empty if unreachable. */
    std::vector<Step> cellsTo(int u, int v) const;

private:
    int index(int u, int v, int f) const { return ((v * eu_) + u) * 4 + (f - 1); }

    const NavModel &nav_;
    int eu_, ev_;
    int startU_, startV_;
    std::vector<double> cost_;
    std::vector<int>    parent_;     // state index, -1 at the start
    std::vector<signed char> how_;   // the move into it: 0 forward, 1 back, 2 right, 3 left
};

}  // namespace bot
