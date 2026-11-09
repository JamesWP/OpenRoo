#include "simbot.h"
#include <chrono>
#include <stdio.h>
#include "actions.h"
#include "planner.h"
#include "logger.h"
#include "moves.h"
#include "world.h"

namespace simbot {

namespace {

/* Without a plan, how often it tries again. */
const unsigned long PLAN_TICKS = 60;

}  // namespace

bool SimBot::Expect::operator==(const Expect &o) const
{
    return now == o.now && u == o.u && v == o.v && h == o.h && facing == o.facing &&
           moveDir == o.moveDir;
}

SimBot::Expect SimBot::expectOf(const World &w)
{
    const Player *p = w.player();
    return {w.now(), p->cellU(), p->cellV(), p->heightCell(), p->facing(), p->moveDir()};
}

void SimBot::plan(const World &world)
{
    const auto t0 = std::chrono::steady_clock::now();
    const Plan p = makePlan(world, actions(), completeLevel());
    planMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    plans_++;
    presses_.clear();
    expect_.clear();
    at_ = 0;
    char buf[160];
    if (!p.found) {
        snprintf(buf, sizeof(buf), "no plan (%d states)", p.expanded);
        note_ = buf;
        planned_.clear();
        return;
    }
    planned_ = p.describe();
    snprintf(buf, sizeof(buf), "plan of %.1f s, %d states, %.0f ms to plan", p.ms / 1000,
             p.expanded, planMs_);
    note_ = buf;
    g_logger.logMessage(1, "SIMBOT: %s: %s", note_.c_str(), planned_.c_str());
    // What the World should be before each press.
    World sim(world);
    for (unsigned press : p.presses) {
        expect_.push_back(expectOf(sim));
        tick(sim, press);
    }
    presses_ = p.presses;
}

unsigned SimBot::act(const bot::BotView &, const World &world)
{
    ticks_++;
    if (at_ < presses_.size() && expectOf(world) == expect_[at_])
        return presses_[at_++];
    if (at_ < presses_.size()) {
        const Expect e = expect_[at_], g = expectOf(world);
        g_logger.logMessage(1, "SIMBOT: off the plan at its tick %zu: expected %.3f (%d,%d,%d) f%d m%d, is %.3f (%d,%d,%d) f%d m%d", at_,
                            e.now, e.u, e.v, e.h, e.facing, e.moveDir, g.now, g.u, g.v, g.h, g.facing, g.moveDir);
        presses_.clear();
    }
    if (!atRest(world))
        return 0;
    if (tried_ && presses_.empty() && ticks_ - lastTry_ < PLAN_TICKS)
        return 0;
    tried_   = true;
    lastTry_ = ticks_;
    plan(world);
    return at_ < presses_.size() ? presses_[at_++] : 0;
}

std::vector<bot::Thought> SimBot::thoughts() const
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%zu of %zu ticks, plan %d", at_, presses_.size(), plans_);
    return {{"note", note_}, {"plan", planned_}, {"progress", buf}};
}

}  // namespace simbot
