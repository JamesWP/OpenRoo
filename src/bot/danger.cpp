#include "danger.h"
#include <stdlib.h>

namespace bot {

namespace {

int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

}  // namespace

Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs, const std::vector<Blast> &extra)
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

    std::vector<Blast> blasts = extra;
    for (const BombView &b : view.bombs)
        blasts.push_back(predictBomb(view, b));
    for (const Blast &b : blasts) {
        if (b.lost)
            continue;
        // The player's cell through the blast's live time.
        for (double t = b.from; t < b.until; t += view.tickMs > 0 ? view.tickMs : 400) {
            const Step at = playerAt(t);
            if (view.inside(at.u, at.v) && b.kills(at.u, at.v, view.cell(at.u, at.v).height, t)) {
                d.hit = true;
                d.t   = t - start;
                d.why = "bomb";
                return d;
            }
        }
    }
    if (view.foes.empty())
        return d;

    sim.load(view);
    for (const Blast &b : extra)
        sim.addBlast(b);
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
    d.killed = sim.killed();
    return d;
}

}  // namespace bot
