#include "motor.h"
#include <cstdlib>
#include "policy.h"
#include "tile.h"

namespace bot {

namespace {

/* At rest with a key held this many ticks and the player still has not moved:
 * the game refuses the move (a wall, a drop it will not take). */
const int BLOCKED_TICKS = 30;
/* Waiting beside no next cell this many ticks (a teleporter takes a second):
 * the player has left the path. */
const int WAIT_TICKS = 120;
/* A sticky pad's hold (movableentity.cpp): the player is held while the clock
 * is within this of the tick it was caught. */
const double STICKY_MS = 3000.0;

unsigned reverse(unsigned acts)
{
    unsigned out = 0;
    if (acts & PRESS_FORWARD) out |= PRESS_BACK;
    if (acts & PRESS_BACK)    out |= PRESS_FORWARD;
    if (acts & PRESS_LEFT)    out |= PRESS_RIGHT;
    if (acts & PRESS_RIGHT)   out |= PRESS_LEFT;
    return out | (acts & PRESS_BOMB);
}

}  // namespace

void Motor::follow(std::vector<Step> path)
{
    path_       = std::move(path);
    next_       = 0;
    status_     = path_.empty() ? Arrived : Walking;
    lastAct_    = 0;
    stillTicks_ = 0;
}

int Motor::direction(int au, int av, int bu, int bv)
{
    const int du = bu - au, dv = bv - av;
    for (int d = DIR_MIN; d <= DIR_MAX; d++)
        if (du == DIR_DU[d] && dv == DIR_DV[d])
            return d;
    return 0;
}

int Motor::facingAfter(int f, const std::vector<Step> &path)
{
    for (size_t i = 1; i < path.size(); i++) {
        const int d = direction(path[i - 1].u, path[i - 1].v, path[i].u, path[i].v);
        if (d == 0)
            continue;   // a forced move: the facing it leaves is not known here
        const int rel = (d - f + 4) % 4;
        if (rel == 1 || rel == 3)
            f = d;
    }
    return f;
}

unsigned Motor::toward(int f, int d)
{
    switch ((d - f + 4) % 4) {
    case 0:  return PRESS_FORWARD;
    case 1:  return PRESS_RIGHT;
    case 2:  return PRESS_BACK;
    default: return PRESS_LEFT;
    }
}

unsigned Motor::act(const BotView &view)
{
    if (status_ != Walking)
        return 0;
    const PlayerView &p = view.player;

    // The player's cell is where it stands, or where its step is going.  A
    // forced move (ice, a slide, a teleporter) may carry it several cells on.
    // Carried along a platform's ride (a leg of cells in a line, on its
    // track), it is on the leg, bound for its end.
    const bool track = view.cell(p.u, p.v).track;
    auto onLeg = [&](Step a, Step b) {
        if (!track)
            return false;
        if (a.u == b.u && a.u == p.u)
            return std::abs(b.v - a.v) > 1 && (p.v - a.v) * (p.v - b.v) < 0;
        if (a.v == b.v && a.v == p.v)
            return std::abs(b.u - a.u) > 1 && (p.u - a.u) * (p.u - b.u) < 0;
        return false;
    };
    for (size_t i = next_; i < path_.size(); i++) {
        if (path_[i].u == p.u && path_[i].v == p.v) {
            next_ = i + 1;
            break;
        }
        if (i + 1 < path_.size() && onLeg(path_[i], path_[i + 1])) {
            next_ = i + 1;
            break;
        }
    }
    if (next_ >= path_.size()) {
        if (p.atRest())
            status_ = Arrived;
        return 0;
    }
    const Cell &here = view.cell(p.u, p.v);
    if (here.kind == TILE_STICKY && !here.busy &&
        (p.stuckSince <= 0 || view.now + view.tickMs - p.stuckSince <= STICKY_MS)) {
        // Held until the pad lets go: the game frees the player and takes
        // the key on the same tick, so it is held from the tick before.
        stillTicks_ = 0;
        return 0;
    }
    const int d = direction(p.u, p.v, path_[next_].u, path_[next_].v);
    if (d == 0) {
        // Not beside the next cell: a teleporter is about to move the player,
        // or it has left the path.
        if (++stillTicks_ > WAIT_TICKS)
            status_ = Blocked;
        return 0;
    }

    // Turning, it faces where the turn goes once it ends, when a key held
    // now is taken.
    unsigned acts = toward(p.turning ? p.moveDir - 10 : p.facing, d);
    if (p.falling && !p.gliding) {
        // Nothing to press in the air, but under a glider.  One about to
        // open takes the key held as it falls on the tick it opens; until
        // then the game drops it (movableentity.cpp).
        stillTicks_ = 0;
        if (p.glides == 0 || p.launched || p.teleport)
            return 0;
        lastAct_ = 0;
        return p.reversed ? reverse(acts) : acts;
    }
    if (p.moving) {
        // Hold only a step, never a turn, ahead of the step's end.
        stillTicks_ = 0;
        acts &= PRESS_FORWARD | PRESS_BACK;
    } else if (lastAct_ != 0) {
        if (++stillTicks_ > BLOCKED_TICKS) {
            status_ = Blocked;
            return 0;
        }
    }
    lastAct_ = acts;
    return p.reversed ? reverse(acts) : acts;
}

}  // namespace bot
