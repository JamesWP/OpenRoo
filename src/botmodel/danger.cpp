#include "danger.h"
#include <string>
#include <stdlib.h>
#include "fallingmodel.h"
#include "liftmodel.h"
#include "nav.h"

namespace bot {

namespace {

const double FALL_MARGIN_MS = 100.0;  // off it this long before it goes
/* A bomb a foe drops is predicted to go off within about this much of when
 * the foe model says: its drop hangs on the foe's steps and the player's,
 * which run a little late. */
const double DROP_MARGIN_MS = 250.0;

/* A thrower this near the player pens it in when, with the throwers' cells
 * shut, the player reaches no more than this many cells, and fewer than
 * with them open.  It is tested this long after the plan's linger, the
 * player staying: one closing the way out then has the player already. */
const int    PEN_NEAR     = 3;
const int    PEN_CELLS    = 20;
const double PEN_AHEAD_MS = 2000.0;

int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

/* The cells the player at rest on at, atMs from the view's clock, can come
 * to rest on, and whether from is one. */
int reachable(const NavModel &nav, Step at, double atMs, Step from, bool *back)
{
    const Paths paths(nav, at.u, at.v, 1, atMs);
    int n = 0;
    for (int v = 0; v < nav.view().extentV; v++)
        for (int u = 0; u < nav.view().extentU; u++)
            n += paths.cost(u, v) >= 0;
    *back = (from.u == at.u && from.v == at.v) || paths.cost(from.u, from.v) >= 0;
    return n;
}

/* Whether the throwers, where the foe model has them, pen in the player at
 * rest on at (PEN_NEAR), come from the cell from outside the pen: it cannot
 * walk through one (movableentity.cpp), and is left a few cells to wait in
 * for their bombs.  A player penned in already is not penned by a walk. */
bool penned(const BotView &view, const FoeModel &sim, Step at, double atMs, Step from)
{
    NavModel shut(view);
    bool near = false;
    for (size_t i = 0; i < sim.foeCount(); i++) {
        near = near || chebyshev(sim.foeCell(i), at) <= PEN_NEAR;
        shut.shut(sim.foeCell(i));
    }
    if (!near)
        return false;
    bool inside, open;
    const int left = reachable(shut, at, atMs, from, &inside);
    return !inside && left <= PEN_CELLS && left < reachable(NavModel(view), at, atMs, from, &open);
}

/* checkPlan; with reach, passing within a thrower's reach is a hit too, as
 * soon as it is (foeMeets). */
Danger check(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
             double lingerMs, const std::vector<Blast> &extra, bool reach)
{
    Danger d;
    if (plan.empty())
        return d;
    const double start = view.now;
    const double arrives = start + plan.back().t;
    const double end     = arrives + lingerMs;
    auto entryAt = [&](double clock) {
        const Timed *at = &plan.front();
        for (const Timed &m : plan)
            if (start + m.t <= clock + 1e-6)
                at = &m;
        return *at;
    };
    auto playerAt = [&](double clock) { return entryAt(clock).cell; };

    // Aloft, the player is out of reach of the foes and the blasts below.

    // Falling tiles: not onto one that is gone, and off each one before it
    // falls (fallingmodel.h).
    for (size_t i = 1; i < plan.size(); i++)
        if (goneAt(view, plan[i].cell, plan[i].t)) {
            d.hit = true;
            d.t   = plan[i].t;
            d.why = "fallen tile";
            return d;
        }
    // A bridge on its way back: its deck is going from under the player (a
    // foe may work its switch).
    for (const BridgeView &b : view.bridges)
        if (b.moving && !b.extended)
            for (const Timed &m : plan)
                for (const Step &c : b.deck)
                    if (c.u == m.cell.u && c.v == m.cell.v) {
                        d.hit = true;
                        d.t   = m.t;
                        d.why = "bridge";
                        return d;
                    }
    for (size_t i = 0; i < plan.size(); i++) {
        const double falls = fallsAt(view, plan[i].cell, plan[i].t);
        if (falls < 0)
            continue;
        const double leave = i + 1 < plan.size() ? plan[i + 1].t : plan.back().t + lingerMs;
        if (leave + FALL_MARGIN_MS > falls) {
            d.hit = true;
            d.t   = falls;
            d.why = "falling tile";
            return d;
        }
    }

    // The player's height on a cell at a clock: on a lift, the lift's.
    auto heightAt = [&](Step at, double clock) {
        const int h = view.cell(at.u, at.v).height;
        const LiftView *lift = liftAt(view, at.u, at.v);
        const int riding = lift ? liftHeightAt(view, *lift, clock - start) : -1;
        return riding >= 0 ? riding : h;
    };
    // The player's cell through a blast's live time.
    auto blasted = [&](const Blast &b) {
        if (b.lost)
            return false;
        for (double t = b.from; t < b.until; t += view.tickMs > 0 ? view.tickMs : 400) {
            const Timed e = entryAt(t);
            const Step at = e.cell;
            if (!e.aloft && view.inside(at.u, at.v) && b.kills(at.u, at.v, heightAt(at, t), t)) {
                d.hit = true;
                d.t   = t - start;
                d.why = "bomb";
                return true;
            }
        }
        return false;
    };
    std::vector<Blast> blasts = extra;
    for (const BombView &b : view.bombs)
        blasts.push_back(predictBomb(view, b));
    for (const Blast &b : blasts)
        if (blasted(b))
            return d;
    if (view.foes.empty())
        return d;

    sim.load(view);
    for (const Blast &b : extra)
        sim.addBlast(b);
    std::vector<Step> before(view.foes.size());   // by foeId
    for (size_t i = 0; i < sim.foeCount(); i++)
        before[sim.foeId(i)] = sim.foeCell(i);
    Step playerBefore = playerAt(start);
    while (sim.now() < end) {
        const double prev = sim.now();
        sim.runTick(playerAt);
        const Timed e = entryAt(sim.now());
        const Step p = e.cell;
        for (size_t i = 0; i < sim.foeCount(); i++) {
            const Step f = sim.foeCell(i);
            const bool catcher = sim.foeKind(i) == 2;
            const bool caught = catcher &&
                ((f.u == p.u && f.v == p.v) ||
                 (f.u == playerBefore.u && f.v == playerBefore.v &&
                  before[sim.foeId(i)].u == p.u && before[sim.foeId(i)].v == p.v));
            // Within a thrower's reach is no harm in passing, which its bombs
            // judge, but no place to stop.
            const bool thrown = (reach || sim.now() >= arrives) && sim.throwsAtPlayer(i) &&
                                chebyshev(f, p) <= 1;
            // A thrower stands in the way: the step into its cell is not
            // taken (movableentity.cpp), and the plan goes wrong.
            const bool blocked = !catcher && f.u == p.u && f.v == p.v &&
                                 (p.u != playerBefore.u || p.v != playerBefore.v);
            if ((caught || thrown || blocked) && !e.aloft) {
                d.hit = true;
                d.t   = sim.now() - start;
                d.why = caught ? "catcher" : thrown ? "thrower" : "blocked";
                return d;
            }
            before[sim.foeId(i)] = f;
        }
        playerBefore = p;
        if (sim.now() == prev)
            break;  // no tick step: nothing moves
    }
    // The foes' bombs and kills within it, before the pen test runs the
    // model on.
    const std::vector<Blast> dropped = sim.dropped();
    const int killed = sim.killed();
    const std::vector<FoeModel::Kill> kills = sim.kills();
    // On the exit with the quota the level is done: nothing pens it in.
    const Step last = plan.back().cell;
    const bool finishes = last.u == view.exitU && last.v == view.exitV;
    if (!reach && !finishes) {
        bool near = false;
        for (size_t i = 0; i < sim.foeCount(); i++)
            near = near || chebyshev(sim.foeCell(i), plan.back().cell) <= PEN_NEAR + 3;
        while (near && sim.now() < end + PEN_AHEAD_MS) {
            const double prev = sim.now();
            sim.runTick(playerAt);
            if (sim.now() == prev)
                break;
        }
        if (near && penned(view, sim, plan.back().cell, sim.now() - start, plan.front().cell)) {
            d.hit = true;
            d.t   = sim.now() - start;
            d.why = "penned";
            return d;
        }
    }
    // A bomb a foe dropped meanwhile (at a catcher, say) may catch the player.
    for (Blast b : dropped) {
        b.from -= DROP_MARGIN_MS;
        b.until += DROP_MARGIN_MS;
        if (blasted(b))
            return d;
    }
    d.killed = killed;
    for (const FoeModel::Kill &k : kills)
        if (k.byAdded)
            d.kills.push_back(k);
        else
            d.foeKills++;
    return d;
}

}  // namespace

Danger checkPlan(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                 double lingerMs, const std::vector<Blast> &extra)
{
    return check(sim, view, plan, lingerMs, extra, false);
}

double foeMeets(FoeModel &sim, const BotView &view, const std::vector<Timed> &plan,
                double horizonMs)
{
    const Danger d = check(sim, view, plan, horizonMs, {}, true);
    const std::string why = d.why;
    return d.hit && (why == "catcher" || why == "thrower") ? d.t : -1;
}

}  // namespace bot
