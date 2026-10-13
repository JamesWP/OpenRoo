/* The motor: walks the player along a path of cells with the tank controls.
 *
 * At rest it steps forward if it faces the next cell, steps back (which moves
 * without turning) if it faces away from it, and otherwise turns the shorter
 * way.  While a step is under way it holds the next step's forward or back
 * already: the game takes a held key on the tick the step ends, so the steps
 * run on without a gap.  It never holds a turn while moving, which the game
 * would queue.  Under the reversed-controls effect it presses the opposite
 * keys.
 *
 * Cells are (u, v), each a neighbour of the one before, except where a forced
 * move carries the player on (ice, a slide, a teleporter): the motor holds
 * nothing while it is carried, and picks the path up from wherever on it the
 * player comes to rest.  It holds nothing on a sticky pad until the pad lets
 * go. */
#pragma once

#include <vector>
#include "botview.h"

namespace bot {

class Motor {
public:
    enum Status { Idle, Walking, Arrived, Blocked };

    /* Starts along path; the first cell may be the player's own. */
    void follow(std::vector<Step> path);

    /* Stops: holds nothing more, so the player comes to rest after the move
     * under way. */
    void halt() { status_ = Idle; }

    /* The presses to hold this tick (policy.h). */
    unsigned act(const BotView &view);

    Status status() const { return status_; }

    /* The facing the player has after walking path from rest facing f: a
     * step along the facing or straight back keeps it, any other turns to
     * face first. */
    static int facingAfter(int f, const std::vector<Step> &path);

    /* The direction (1..4) from a to its neighbour b, or 0 if b is not one. */
    static int direction(int au, int av, int bu, int bv);

private:
    /* The action that moves from facing f towards direction d, unreversed. */
    static unsigned toward(int f, int d);

    std::vector<Step> path_;
    size_t   next_ = 0;
    Status   status_ = Idle;
    unsigned lastAct_ = 0;
    int      stillTicks_ = 0;  // ticks at rest with a key held and nothing moving
};

}  // namespace bot
