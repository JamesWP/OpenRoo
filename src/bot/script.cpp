#include "script.h"
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
            if (view.now < s.untilMs)
                return 0;
            next();
            continue;
        case ScriptStep::Turn: {
            if (!p.atRest())
                return 0;
            if (p.facing == s.facing) {
                next();
                continue;
            }
            if (ticks_ > STEP_TICKS) {
                status_ = Failed;
                return 0;
            }
            const unsigned a = (s.facing - p.facing + 4) % 4 == 3 ? ACT_LEFT : ACT_RIGHT;
            return p.reversed ? (a == ACT_LEFT ? ACT_RIGHT : ACT_LEFT) : a;
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
            return p.atRest() && view.now >= p.bombReadyAt ? (unsigned)ACT_BOMB : 0u;
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
            return motor_.act(view);
        }
    }
}

}  // namespace bot
