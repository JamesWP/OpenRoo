/* Bait(c): with a thrower about that bombs the player, the player standing
 * on c, where the thrower's bomb lands (board.h, baits): the blocks it spends,
 * no bomb of the player's.  Its cost is a guess: when the thrower comes up
 * only the foe model says.
 * Done as a walk to c, by way of a cell near the thrower if it has to be
 * drawn off first (and a wait there), a wait on c for the bomb the foe model
 * has the thrower drop, and a walk clear of it the moment it is dropped; the
 * walk is taken only where the foe model has a bomb spend a block and the
 * player stays safe through its blast (danger.h).  With such a bomb dropped
 * already, a walk clear of it and a wait for it to blow. */
#include "action.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a bait to cost, past the walk: the thrower's coming
 * and the blast. */
const double BAIT_MS = 10000;
/* How long the player waits on the cell for the thrower's bomb, at most. */
const double WAIT_MS = 15000;
/* The cells to draw a thrower off from: this many cells from it, the nearest
 * LURES of them, with these waits there. */
const int    LURE_NEAR = 3;
const size_t LURES = 6;
const double LURE_WAITS[] = {0, 2000};
/* The waits before setting off for the bait, not drawing a thrower first. */
const double SET_OFF_WAITS[] = {0, 1500, 3000, 5000};
/* A thrower drawn off is led on to the bait a leg at a time, of this many of
 * the way's turning points (Paths::cellsTo), the player waiting after each
 * until one comes within LEAD_NEAR, for LEAD_WAIT_MS at most: the chase of a
 * player further off may give up (foe.cpp, the search's cap). */
const int    LEAD_LEG = 3;
const int    LEAD_NEAR = 4;
const double LEAD_WAIT_MS = 6000;
/* How long the player waits beside a teleporter for a thrower to go through. */
const double SEND_WAIT_MS = 15000;
/* A thrower led to the bait may be baited from a cell this near it instead. */
const int    BESIDE = 2;
/* The cells to walk away to: the nearest RETREATS (walk.h). */
const size_t RETREATS = 24;
/* With no bait to be had yet, the player waits on the bait cell this long,
 * drawing a thrower on, where the foe model has one come within DRAW_NEAR
 * of it in DRAW_COME_MS. */
const double DRAW_MS = 800;
const double DRAW_COME_MS = 40000;
const int    DRAW_NEAR = 2;

/* A teleporter's tile kind (TILE_TELEPORTER, tile.h). */
const int TELEPORTER = 0x0f;

int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

/* before, with a walk from at, startMs from now, to the nearest cell where
 * the player is safe through blast, and a wait until it has blown. */
bool clearOf(Situation &s, const Option &before, Step at, int facing, double startMs,
             const Blast &blast, Option *out)
{
    const BotView &view = s.view;
    const RoutesOn away(s.nav, before.schedule, at, facing, startMs, Bound{RETREAT_MS});
    for (const Step &to : retreatCells(view, away.paths(), RETREATS, true)) {
        Option o = before;
        Option walk;
        if (!walkTo(s, away.paths(), to, 0, walk))
            continue;
        append(o, walk);
        const double done = blast.until + CLEAR_MARGIN_MS;
        o.script.push_back({ScriptStep::Wait, 0, {}, done});
        o.lingerMs = std::max(1000.0, done - view.now - o.schedule.back().t);
        if (checkEscape(s.foes, view, o.schedule, o.lingerMs).hit)
            continue;
        o.checked = true;
        o.stepMs  = done - view.now;
        o.cost    = o.stepMs;
        char buf[48];
        snprintf(buf, sizeof(buf), ", away to (%d,%d)", to.u, to.v);
        o.what += buf;
        *out = std::move(o);
        return true;
    }
    return false;
}

/* going walked on to end, from where it stands (Paths on), and a wait there
 * until the foe model has a thrower that bombs the player come within near of
 * by (waitMs at most).  False, going as it was, if not. */
bool walkAndWait(Situation &s, Option &going, const Paths &on, Step end, Step by, int near,
                 double waitMs)
{
    const BotView &view = s.view;
    Option o = going;
    Option leg;
    if (!walkTo(s, on, end, 0, leg))
        return false;
    append(o, leg);
    if (near >= 0) {
        const std::vector<Timed> &plan = o.schedule;
        auto playerAt = keeping(plan, view.now);
        s.foes.load(view);
        double came = -1;
        while (came < 0 && s.foes.now() < view.now + o.cost + waitMs) {
            const double prev = s.foes.now();
            s.foes.runTick(playerAt);
            if (s.foes.now() == prev)
                return false;
            if (s.foes.now() < view.now + o.cost)
                continue;
            for (size_t i = 0; i < s.foes.foeCount(); i++)
                if (s.foes.throwsAtPlayer(i) && chebyshev(s.foes.foeCell(i), by) <= near)
                    came = s.foes.now();
        }
        if (came < 0)
            return false;
        if (came - view.now > o.cost + 1) {
            o.cost = came - view.now;
            o.script.push_back({ScriptStep::Wait, 0, {}, came});
            o.schedule.push_back({o.cost, end});
        }
    }
    going = std::move(o);
    return true;
}

/* The cells around a cell, the four sides first. */
const int AROUND_DU[8] = {0, 1, 0, -1, 1, 1, -1, -1}, AROUND_DV[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

/* going, which ends on from, walked on to bait leading a thrower (LEAD_LEG):
 * false if none keeps up. */
bool leadOn(Situation &s, Option &going, Step from, Step bait)
{
    const BotView &view = s.view;
    Step at = from;
    std::set<std::pair<int, int>> sentThrough;
    for (int legs = 0; at.u != bait.u || at.v != bait.v; legs++) {
        if (legs > 64)
            return false;
        const RoutesOn routes(s.nav, going.schedule, at, facingAfter(view.player.facing, going.script),
                              going.cost);
        const Paths &on = routes.paths();
        const std::vector<Step> cells = on.cellsTo(bait.u, bait.v);
        if (cells.size() < 2)
            return false;
        // A teleporter on the way: the thrower sent through it first, the
        // player standing beside it, where the thrower's chase goes over it
        // (foemodel.h), and then the player after it.
        bool through = false;
        for (size_t i = 1; i + 1 < cells.size(); i++) {
            const Cell &t = view.cell(cells[i].u, cells[i].v);
            // Either end of a pad the thrower went through already: the
            // player arriving at the other end is no new one.
            if (t.kind != TELEPORTER || !sentThrough.insert({cells[i].u, cells[i].v}).second)
                continue;
            sentThrough.insert({t.teleU, t.teleV});
            for (int d = 0; d < 8 && !through; d++) {
                const Step b{cells[i].u + AROUND_DU[d], cells[i].v + AROUND_DV[d]};
                if (!view.inside(b.u, b.v) || view.cell(b.u, b.v).kind == TELEPORTER ||
                    view.cell(b.u, b.v).height != t.height || on.cost(b.u, b.v) < 0)
                    continue;
                Option o = going;
                if (walkAndWait(s, o, on, b, {t.teleU, t.teleV}, 1, SEND_WAIT_MS)) {
                    going = std::move(o);
                    at = b;
                    through = true;
                }
            }
            break;
        }
        if (through)
            continue;
        const Step end = cells[std::min(cells.size() - 1, (size_t)LEAD_LEG)];
        const bool last = end.u == bait.u && end.v == bait.v;
        if (!walkAndWait(s, going, on, end, end, last ? -1 : LEAD_NEAR, LEAD_WAIT_MS))
            return false;
        at = end;
    }
    return true;
}

/* going: the walk to the bait cell, facing facing there.  The first bomb the
 * foe model has a thrower drop that spends a block, the player waiting on the
 * cell; false if none does in WAIT_MS, or it is dropped before the player is
 * there. */
bool baitedBomb(Situation &s, const Option &going, Blast *out)
{
    const BotView &view = s.view;
    const std::vector<Timed> &plan = going.schedule;
    s.foes.load(view);
    s.foes.run(view.now + going.cost + WAIT_MS, keeping(plan, view.now));
    for (const Blast &b : s.foes.dropped())
        if (spendsBlock(view, b)) {
            if (b.from - FUSE_MS - view.now < going.cost - view.tickMs)
                return false;
            *out = b;
            return true;
        }
    return false;
}

/* For a thrower led to the bait, or come up already: a walk on from it to a
 * cell within BESIDE of it where the foe model has the thrower's bomb spend
 * a block (baitedBomb), appended to going, offered to take with the cell and
 * the blast, in turn until it takes one.  False if it takes none. */
bool besideBait(Situation &s, const Option &going, Step bait,
                const std::function<bool(const Option &, Step, const Blast &)> &take)
{
    const BotView &view = s.view;
    const RoutesOn routes(s.nav, going.schedule, bait, facingAfter(view.player.facing, going.script),
                          going.cost);
    const Paths &on = routes.paths();
    for (int v = bait.v - BESIDE; v <= bait.v + BESIDE; v++)
        for (int u = bait.u - BESIDE; u <= bait.u + BESIDE; u++) {
            if (!view.inside(u, v) || (u == bait.u && v == bait.v) || on.cost(u, v) < 0 ||
                view.cell(u, v).kind == TELEPORTER || NavModel::moving(view.cell(u, v)))
                continue;
            Option o = going;
            Option leg;
            if (!walkTo(s, on, {u, v}, 0, leg))
                continue;
            append(o, leg);
            Blast blast;
            if (baitedBomb(s, o, &blast) && take(o, {u, v}, blast))
                return true;
        }
    return false;
}

class BaitAction : public Action {
public:
    const char *name() const override { return "bait"; }

    /* The cheapest bait cell for each set of blocks. */
    void expand(const Facts &f, double, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        if ((f.changes & board.allBlocks()) == board.allBlocks())
            return;
        std::map<Changes, std::pair<double, Place>> best;
        for (const Site &site : board.baits(f.changes, f.throwerAt)) {
            const double t = board.time(f.changes, f.at, {site.cell.u, site.cell.v});
            if (t < 0)
                continue;
            // The thrower comes up as well.
            const double ms = t + BAIT_MS + site.far * THROWER_STEP_MS;
            auto it = best.find(site.hits);
            if (it == best.end() || ms < it->second.first)
                best[site.hits] = {ms, site.cell};
        }
        for (const auto &[hits, c] : best) {
            Successor n{f, {}};
            n.facts.changes |= hits;
            if (!board.worlds().has(n.facts.changes) && board.worlds().size() > options.maxWorlds)
                continue;
            n.facts.at = c.second;
            n.step.end   = c.second;
            n.step.heads = {c.second.u, c.second.v};
            n.step.ms    = c.first;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlayerView &p = view.player;
        if (p.falling)
            return;
        const Step here{p.u, p.v};
        const Step bait{plan.steps.front().end.u, plan.steps.front().end.v};
        // Dropped already: clear of it until it blows.
        for (const BombView &b : view.bombs) {
            const Blast blast = predictBomb(view, b);
            if (!spendsBlock(view, blast))
                continue;
            Option before;
            before.what = "clear of a thrower's bomb";
            before.schedule.push_back({0, here});
            Option o;
            if (clearOf(s, before, here, p.facing, s.from.startMs(), blast, &o))
                out.push_back(std::move(o));
            return;
        }
        // The cells to draw a thrower off from first, if it is not coming.
        std::vector<std::pair<double, Step>> lures{{0, {-1, -1}}};
        {
            std::vector<std::pair<double, Step>> near;
            for (int v = 0; v < view.extentV; v++)
                for (int u = 0; u < view.extentU; u++) {
                    const double c = s.from.cost(u, v);
                    if (c <= 0 || NavModel::moving(view.cell(u, v)))
                        continue;
                    for (const FoeView &f : view.foes)
                        if (f.kind == 3 && !f.held && chebyshev({f.u, f.v}, {u, v}) == LURE_NEAR) {
                            near.push_back({c, {u, v}});
                            break;
                        }
                }
            std::stable_sort(near.begin(), near.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            for (size_t i = 0; i < near.size() && i < LURES; i++)
                lures.push_back(near[i]);
        }
        // Each lure with each wait there, and led on from it.
        std::vector<std::pair<double, bool>> ways;
        for (const double wait : LURE_WAITS)
            ways.push_back({wait, false});
        ways.push_back({0, true});
        // Not drawn: after a wait where the player stands, for a thrower
        // just sent through a pad to walk off its far end (send.cpp).
        std::vector<std::pair<double, bool>> waits;
        for (const double wait : SET_OFF_WAITS)
            waits.push_back({wait, false});
        for (const auto &[lureMs, lure] : lures) {
            const bool drawn = lure.u >= 0;
            for (const auto &[wait, led] : drawn ? ways : waits) {
                Option going;
                char buf[96];
                if (drawn) {
                    if (!walkTo(s, s.from, lure, 0, going))
                        continue;
                    if (wait > 0 && !led) {
                        going.cost += wait;
                        going.script.push_back({ScriptStep::Wait, 0, {}, view.now + going.cost});
                        going.schedule.push_back({going.cost, lure});
                    }
                    if (led) {
                        if (!leadOn(s, going, lure, bait))
                            continue;
                    } else if ((bait.u != lure.u || bait.v != lure.v) && !walkOn(s, s.nav, going, lure, bait)) {
                        continue;
                    }
                    snprintf(buf, sizeof(buf), "bait at (%d,%d), %s from (%d,%d) after %.0f ms",
                             bait.u, bait.v, led ? "led" : "drawn", lure.u, lure.v, wait);
                } else {
                    if (bait.u == here.u && bait.v == here.v) {
                        going.schedule.push_back({0, here});
                        going.cost = s.from.startMs() + wait;
                    } else if (wait > 0) {
                        const Paths later(s.nav, here.u, here.v, p.facing, s.from.startMs() + wait);
                        if (!walkTo(s, later, bait, 0, going))
                            continue;
                        going.script.insert(going.script.begin(), {ScriptStep::Wait, 0, {}, view.now + s.from.startMs() + wait});
                        going.schedule.insert(going.schedule.begin(), {0, here});
                    } else if (!walkTo(s, s.from, bait, 0, going)) {
                        continue;
                    }
                    snprintf(buf, sizeof(buf), "bait at (%d,%d) after %.0f ms", bait.u, bait.v, wait);
                }
                // On the cell till the bomb is dropped, then clear of it.
                going.what = buf;
                auto take = [&](const Option &on, Step stand, const Blast &blast) {
                    Option o = on;
                    const double leave = blast.from - FUSE_MS - view.now + view.tickMs;
                    if (leave > o.cost + 1)
                        o.script.push_back({ScriptStep::Wait, 0, {}, view.now + leave});
                    o.schedule.push_back({std::max(leave, o.cost), stand});
                    Option cleared;
                    if (!clearOf(s, o, stand, facingAfter(p.facing, o.script), std::max(leave, o.cost),
                                 blast, &cleared))
                        return false;
                    out.push_back(std::move(cleared));
                    return true;
                };
                Blast blast;
                if (baitedBomb(s, going, &blast) && take(going, bait, blast))
                    return;
                // Led there, or come up already, the thrower may stand where
                // its bomb at a cell beside the bait spends the block instead.
                if ((led || (!drawn && wait == 0)) && besideBait(s, going, bait, take))
                    return;
            }
        }
        drawTo(s, bait, out);
    }

private:
    /* None yet: on to the bait cell and a wait there, the thrower following
     * (as send.cpp), where the foe model has it come up. */
    static void drawTo(Situation &s, Step bait, std::vector<Option> &out)
    {
        const BotView &view = s.view;
        const Step here{view.player.u, view.player.v};
        Option o;
        if (bait.u == here.u && bait.v == here.v) {
            o.schedule.push_back({0, here});
            o.cost = s.from.startMs();
        } else if (!walkTo(s, s.from, bait, 0, o)) {
            return;
        }
        bool coming = false;
        s.foes.load(view);
        while (!coming && s.foes.now() < view.now + o.cost + DRAW_COME_MS && view.tickMs > 0) {
            s.foes.runTick(keeping(o.schedule, view.now));
            for (size_t i = 0; i < s.foes.foeCount() && !coming; i++)
                coming = s.foes.throwsAtPlayer(i) && chebyshev(s.foes.foeCell(i), bait) <= DRAW_NEAR;
        }
        if (!coming)
            return;
        o.cost += DRAW_MS;
        o.script.push_back({ScriptStep::Wait, 0, {}, view.now + o.cost});
        o.schedule.push_back({o.cost, bait});
        o.lingerMs = DRAW_MS;
        if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
            return;
        o.checked = true;
        o.stepMs  = o.cost;
        char buf[80];
        snprintf(buf, sizeof(buf), "draw a thrower to bait at (%d,%d)", bait.u, bait.v);
        o.what = buf;
        out.push_back(std::move(o));
    }
};

}  // namespace

const Action &baitAction()
{
    static const BaitAction action;
    return action;
}

}  // namespace bot
