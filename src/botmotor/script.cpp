#include "script.h"
#include "tile.h"
#include "policy.h"

namespace bot {

namespace {

/* Ticks a Turn or a Drop may take before the step counts as failed. */
const int STEP_TICKS = 180;

}  // namespace

void Executor::start(Script script)
{
    script_ = std::move(script);
    at_ = 0;
    status_ = script_.empty() ? Done : Running;
    stepStarted_ = false;
    ticks_ = 0;
}

void Executor::halt()
{
    motor_.halt();
    if (status_ == Running)
        status_ = Failed;
}

void Executor::next()
{
    at_++;
    stepStarted_ = false;
    ticks_ = 0;
    if (at_ >= script_.size())
        status_ = Done;
}

bool Executor::dropNext(int facing) const
{
    for (size_t i = at_ + 1; i < script_.size(); i++) {
        if (script_[i].kind == ScriptStep::Drop)
            return true;
        if (script_[i].kind != ScriptStep::Turn || script_[i].facing != facing)
            return false;
    }
    return false;
}

bool Executor::endsOnPlain(const BotView &view)
{
    const PlayerView &p = view.player;
    const Cell &c = view.cell(p.u, p.v);
    return p.moving && !p.turning && !p.falling &&
           c.kind == TILE_KIND_01 &&
           p.animStart + p.animMs <= view.now + view.tickMs + 1e-6;
}

unsigned Executor::act(const BotView &view)
{
    if (status_ != Running)
        return 0;
    const PlayerView &p = view.player;
    for (;;) {
        if (status_ != Running)
            return 0;
        const ScriptStep &s = script_[at_];
        ticks_++;
        switch (s.kind) {
        case ScriptStep::Wait:
            // A key pressed on this tick moves the player on the next.
            if (view.now + view.tickMs < s.untilMs - 1e-6)
                return 0;
            next();
            continue;
        case ScriptStep::Turn: {
            if (!p.atRest()) {
                // The turn to it ends on the next tick: on to what follows,
                // so that a bomb is pressed for that tick.
                if (p.turning && !p.falling && p.moveDir - 10 == s.facing &&
                    p.animStart + p.animMs <= view.now + view.tickMs + 1e-6) {
                    next();
                    continue;
                }
                // Facing it already, a step ending on the next tick before a drop.
                if (p.facing == s.facing && endsOnPlain(view) && dropNext(p.facing)) {
                    next();
                    continue;
                }
                return 0;
            }
            if (p.facing == s.facing) {
                next();
                continue;
            }
            if (ticks_ > STEP_TICKS) {
                status_ = Failed;
                return 0;
            }
            const unsigned a = (s.facing - p.facing + 4) % 4 == 3 ? PRESS_LEFT : PRESS_RIGHT;
            return p.reversed ? (a == PRESS_LEFT ? PRESS_RIGHT : PRESS_LEFT) : a;
        }
        case ScriptStep::Drop:
            if (!stepStarted_) {
                stepStarted_ = true;
                bombsBefore_ = p.bombs;
            }
            if (p.bombs < bombsBefore_) {
                next();
                continue;
            }
            if (ticks_ > STEP_TICKS) {
                status_ = Failed;
                return 0;
            }
            {
                // Pressed for the next tick, which takes it once the move
                // under way has ended on it: the bomb drops on its last tick
                // (player.cpp, playtick.cpp).
                const bool ends = p.atRest() ||
                    (p.moving && !p.falling && p.animStart + p.animMs <= view.now + view.tickMs + 1e-6);
                return ends && view.now >= p.bombReadyAt ? (unsigned)PRESS_BOMB : 0u;
            }
        case ScriptStep::StepOff: {
            if (p.u == s.path[1].u && p.v == s.path[1].v) {
                next();
                continue;
            }
            const int d = Motor::direction(s.path[0].u, s.path[0].v, s.path[1].u, s.path[1].v);
            const bool back = (d - p.facing + 4) % 4 == 2;
            if (view.now > s.untilMs || (d != p.facing && !back)) {
                status_ = Failed;
                return 0;
            }
            const unsigned a = back ? PRESS_BACK : PRESS_FORWARD;
            return p.reversed ? (a == PRESS_FORWARD ? PRESS_BACK : PRESS_FORWARD) : a;
        }
        case ScriptStep::Walk:
            if (!stepStarted_) {
                stepStarted_ = true;
                motor_.follow(s.path);
            }
            if (motor_.status() == Motor::Arrived) {
                next();
                continue;
            }
            if (motor_.status() != Motor::Walking) {
                status_ = Failed;
                return 0;
            }
            {
                const unsigned acts = motor_.act(view);
                // Arrived on this tick: what follows is pressed on it too.
                if (motor_.status() == Motor::Arrived) {
                    next();
                    continue;
                }
                // The last step ends on the next tick, and a bomb is to drop
                // there: pressed for that tick.
                const Step &last = s.path.back();
                if (p.u == last.u && p.v == last.v && endsOnPlain(view) && dropNext(p.facing)) {
                    motor_.halt();
                    next();
                    continue;
                }
                return acts;
            }
        }
    }
}

}  // namespace bot
