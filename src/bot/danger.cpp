#include "danger.h"
#include <stdlib.h>

namespace bot {

namespace {

const double BOMB_FUSE_MS = 2000.0;  // bomb.cpp: a 2 s fuse,
const double BOMB_LIVE_MS = 400.0;   // then a 0.4 s kill zone

int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

}  // namespace

Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs)
{
    Danger d;
    if (plan.empty())
        return d;
    const double start = view.now;
    const double end   = start + plan.back().t + lingerMs;
    auto playerAt = [&](double clock) {
        Step at = plan.front().cell;
        for (const Timed &m : plan)
            if (start + m.t <= clock + 1e-6)
                at = m.cell;
        return at;
    };

    for (const BombView &b : view.bombs) {
        const double blowsUntil = b.droppedAt + BOMB_FUSE_MS + BOMB_LIVE_MS;
        for (const Timed &m : plan)
            if (start + m.t < blowsUntil && chebyshev(m.cell, {b.u, b.v}) <= 2) {
                d.hit = true;
                d.t   = m.t;
                d.why = "bomb";
                return d;
            }
    }
    if (view.foes.empty())
        return d;

    sim.load(view);
    std::vector<Step> before;
    for (size_t i = 0; i < sim.foeCount(); i++)
        before.push_back(sim.foeCell(i));
    Step playerBefore = playerAt(start);
    while (sim.now() < end) {
        const double prev = sim.now();
        sim.runTick(playerAt);
        const Step p = playerAt(sim.now());
        if (sim.foeCount() != before.size())
            before.resize(sim.foeCount(), {-100, -100});
        for (size_t i = 0; i < sim.foeCount(); i++) {
            const Step f = sim.foeCell(i);
            const bool catcher = sim.foeKind(i) == 2;
            const bool caught = catcher &&
                ((f.u == p.u && f.v == p.v) ||
                 (f.u == playerBefore.u && f.v == playerBefore.v &&
                  before[i].u == p.u && before[i].v == p.v));
            const bool thrown = !catcher && chebyshev(f, p) <= 1;
            if (caught || thrown) {
                d.hit = true;
                d.t   = sim.now() - start;
                d.why = caught ? "catcher" : "thrower";
                return d;
            }
            before[i] = f;
        }
        playerBefore = p;
        if (sim.now() == prev)
            break;  // no tick step: nothing moves
    }
    return d;
}

}  // namespace bot
