#include "motor.h"
#include "policy.h"
#include "tile.h"

namespace bot {

namespace {

/* At rest with a key held this many ticks and the player still has not moved:
 * the game refuses the move (a wall, a drop it will not take). */
const int BLOCKED_TICKS = 30;

unsigned reverse(unsigned acts)
{
    unsigned out = 0;
    if (acts & ACT_FORWARD) out |= ACT_BACK;
    if (acts & ACT_BACK)    out |= ACT_FORWARD;
    if (acts & ACT_LEFT)    out |= ACT_RIGHT;
    if (acts & ACT_RIGHT)   out |= ACT_LEFT;
    return out | (acts & ACT_BOMB);
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
    case 0:  return ACT_FORWARD;
    case 1:  return ACT_RIGHT;
    case 2:  return ACT_BACK;
    default: return ACT_LEFT;
    }
}

unsigned Motor::act(const BotView &view)
{
    if (status_ != Walking)
        return 0;
    const PlayerView &p = view.player;

    // The player's cell is where it stands, or where its step is going.
    while (next_ < path_.size() && path_[next_].u == p.u && path_[next_].v == p.v)
        next_++;
    if (next_ >= path_.size()) {
        if (!p.moving)
            status_ = Arrived;
        return 0;
    }
    const int d = direction(p.u, p.v, path_[next_].u, path_[next_].v);
    if (d == 0) {
        status_ = Blocked;  // off the path: the player is not beside the next cell
        return 0;
    }

    unsigned acts = toward(p.facing, d);
    if (p.moving) {
        // Hold only a step, never a turn, ahead of the step's end.
        stillTicks_ = 0;
        acts &= ACT_FORWARD | ACT_BACK;
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
