#include "motor.h"
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
    for (size_t i = next_; i < path_.size(); i++)
        if (path_[i].u == p.u && path_[i].v == p.v) {
            next_ = i + 1;
            break;
        }
    if (next_ >= path_.size()) {
        if (p.atRest())
            status_ = Arrived;
        return 0;
    }
    const Cell &here = view.cell(p.u, p.v);
    if (here.kind == TILE_STICKY && !here.busy) {
        stillTicks_ = 0;  // held until the pad lets go
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

    unsigned acts = toward(p.facing, d);
    if (p.falling) {
        stillTicks_ = 0;  // nothing to press in the air
        return 0;
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
