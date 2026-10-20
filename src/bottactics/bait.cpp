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
#include <map>
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
/* The cells to walk away to: within this many ms of the bait cell, the
 * nearest RETREATS of them. */
const double RETREAT_MS = 2500;
const size_t RETREATS = 24;
/* A wait for a blast ends this long after its zone clears. */
const double CLEAR_MARGIN_MS = 200;

int chebyshev(Step a, Step b)
{
    const int du = abs(a.u - b.u), dv = abs(a.v - b.v);
    return du > dv ? du : dv;
}

/* The cells within RETREAT_MS of paths' start, nearest first. */
std::vector<Step> retreatCells(const BotView &view, const Paths &paths)
{
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = paths.cost(u, v);
            if (c > 0 && c <= RETREAT_MS && !NavModel::moving(view.cell(u, v)))
                cells.push_back({c, {u, v}});
        }
    std::stable_sort(cells.begin(), cells.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<Step> out;
    for (size_t i = 0; i < cells.size() && i < RETREATS; i++)
        out.push_back(cells[i].second);
    return out;
}

/* before, with a walk from at, startMs from now, to the nearest cell where
 * the player is safe through blast, and a wait until it has blown. */
bool clearOf(Situation &s, const Option &before, Step at, int facing, double startMs,
             const Blast &blast, Option *out)
{
    const BotView &view = s.view;
    NavModel after = s.nav;
    for (const Timed &t : before.schedule)
        after.arm(t.cell, t.t);
    const Paths away(after, at.u, at.v, facing, startMs);
    for (const Step &to : retreatCells(view, away)) {
        Option o = before;
        Option walk;
        if (!walkTo(s, away, to, 0, walk))
            continue;
        o.script.insert(o.script.end(), walk.script.begin(), walk.script.end());
        o.schedule.insert(o.schedule.end(), walk.schedule.begin() + 1, walk.schedule.end());
        const double done = blast.until + CLEAR_MARGIN_MS;
        o.script.push_back({ScriptStep::Wait, 0, {}, done});
        o.lingerMs = std::max(1000.0, done - view.now - o.schedule.back().t);
        if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
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

/* going: the walk to the bait cell, facing facing there.  The first bomb the
 * foe model has a thrower drop that spends a block, the player waiting on the
 * cell; false if none does in WAIT_MS, or it is dropped before the player is
 * there. */
bool baitedBomb(Situation &s, const Option &going, Blast *out)
{
    const BotView &view = s.view;
    const std::vector<Timed> &plan = going.schedule;
    s.foes.load(view);
    s.foes.run(view.now + going.cost + WAIT_MS, [&](double clock) {
        Step at = plan.front().cell;
        for (const Timed &m : plan)
            if (view.now + m.t <= clock + 1e-6)
                at = m.cell;
        return at;
    });
    for (const Blast &b : s.foes.dropped())
        if (spendsBlock(view, b)) {
            if (b.from - FUSE_MS - view.now < going.cost - view.tickMs)
                return false;
            *out = b;
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
        for (const Site &site : board.baits(f.changes)) {
            const double t = board.time(f.changes, f.at, {site.cell.u, site.cell.v});
            if (t < 0)
                continue;
            auto it = best.find(site.hits);
            if (it == best.end() || t + BAIT_MS < it->second.first)
                best[site.hits] = {t + BAIT_MS, site.cell};
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
        for (const auto &[lureMs, lure] : lures) {
            for (const double wait : LURE_WAITS) {
                const bool drawn = lure.u >= 0;
                if (!drawn && wait > 0)
                    break;
                if (wait > 0 && s.nav.timed())
                    break;   // the lifts keep their own time (walk.h)
                Option going;
                int facing = p.facing;
                char buf[96];
                if (drawn) {
                    if (!walkTo(s, s.from, lure, 0, going))
                        continue;
                    facing = Motor::facingAfter(facing, s.from.cellsTo(lure.u, lure.v));
                    if (wait > 0) {
                        going.cost += wait;
                        going.script.push_back({ScriptStep::Wait, 0, {}, view.now + going.cost});
                        going.schedule.push_back({going.cost, lure});
                    }
                    NavModel after = s.nav;
                    for (const Timed &t : going.schedule)
                        after.arm(t.cell, t.t);
                    const Paths on(after, lure.u, lure.v, facing, going.cost);
                    Option leg;
                    if (bait.u != lure.u || bait.v != lure.v) {
                        if (!walkTo(s, on, bait, 0, leg))
                            continue;
                        going.script.insert(going.script.end(), leg.script.begin(), leg.script.end());
                        going.schedule.insert(going.schedule.end(), leg.schedule.begin() + 1,
                                              leg.schedule.end());
                        going.cost = leg.cost;
                        facing = Motor::facingAfter(facing, on.cellsTo(bait.u, bait.v));
                    }
                    snprintf(buf, sizeof(buf), "bait at (%d,%d), drawn from (%d,%d) after %.0f ms",
                             bait.u, bait.v, lure.u, lure.v, wait);
                } else {
                    if (bait.u == here.u && bait.v == here.v) {
                        going.schedule.push_back({0, here});
                        going.cost = s.from.startMs();
                    } else {
                        if (!walkTo(s, s.from, bait, 0, going))
                            continue;
                        facing = Motor::facingAfter(facing, s.from.cellsTo(bait.u, bait.v));
                    }
                    snprintf(buf, sizeof(buf), "bait at (%d,%d)", bait.u, bait.v);
                }
                Blast blast;
                if (!baitedBomb(s, going, &blast))
                    continue;
                // Off the moment it is dropped.
                const double leave = blast.from - FUSE_MS - view.now + view.tickMs;
                if (leave > going.cost + 1)
                    going.script.push_back({ScriptStep::Wait, 0, {}, view.now + leave});
                going.schedule.push_back({std::max(leave, going.cost), bait});
                going.what = buf;
                Option o;
                if (clearOf(s, going, bait, facing, std::max(leave, going.cost), blast, &o)) {
                    out.push_back(std::move(o));
                    return;
                }
            }
        }
    }
};

}  // namespace

const Action &baitAction()
{
    static const BaitAction action;
    return action;
}

}  // namespace bot
