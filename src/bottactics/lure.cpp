/* Lure(k): a bridge extended by a catcher, drawn onto a switch of its slot
 * by its chase of the player, where the player cannot work the switch and go
 * on (one on a pillar a jump pad lands on, for one, whose only way off is the
 * pad, which throws the player on to a drop).  Only with a catcher about, and
 * where extending the bridge lets the player come to rest somewhere new;
 * which catchers can get to the switch the planner does not ask, as where
 * the player goes decides it.  Its cost is a guess: whether and when the
 * catcher is carried onto the switch only the foe model says.
 * Done as a walk to a nearby cell, or one across a cell in line beyond a
 * sticky pad from a catcher (which lets the catcher onto the pad,
 * foemodel.cpp's stepLegal) and on, and a wait at its end until the foe
 * model has a catcher work the switch and the bridge is done: the one whose
 * bridge is done soonest, safe.  A state of the player, the foes and the
 * bridges found to have no lure is not tried again for a while. */
#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include "action.h"
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a lure to cost, past the bridge's move. */
const double LURE_MS = 15000;
/* How long the player waits on the cell for a catcher to work the switch. */
const double WAIT_MS = 12000;
/* The cells to stand on: within this many ms of the player, the nearest
 * CELLS of them. */
const double REACH_MS = 8000;
const size_t CELLS = 40;
/* Or across a cell that lets a catcher off a sticky pad, to one of the
 * nearest SECONDS beyond, and on to one of the nearest SECONDS from there. */
const size_t SECONDS = 16;
/* The states remembered as having no lure, at most, and for how long: the
 * foes' timers (a sticky pad's hold, a step's) move on while they stand. */
const size_t FAILED = 4096;
const double RETRY_MS = 2000;
/* The wait past the move's end, for the last deck cell to be stamped. */
const double SETTLE_MS = 100;

/* The cells paths' start can come to rest on within REACH_MS, not moving
 * and walked back from to the player (not a switch a jump pad throws the
 * player onto, for one), the nearest most of them.  back keeps, by cell,
 * whether it can be walked back from: 1, 0, or -1 not known yet. */
std::vector<Step> nearest(const Situation &s, const Paths &paths, size_t most,
                          std::vector<signed char> &back)
{
    const BotView &view = s.view;
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = paths.cost(u, v) - paths.startMs();
            if (c > 0 && c <= REACH_MS && !NavModel::moving(view.cell(u, v)))
                cells.push_back({c, {u, v}});
        }
    std::stable_sort(cells.begin(), cells.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<Step> out;
    for (size_t i = 0; i < cells.size() && out.size() < most; i++) {
        const Step c = cells[i].second;
        signed char &ok = back[(size_t)c.v * view.extentU + c.u];
        if (ok < 0)
            ok = Paths(s.nav, c.u, c.v, 1).cost(view.player.u, view.player.v) >= 0;
        if (ok)
            out.push_back(c);
    }
    return out;
}

/* When bridge k is done, the clock's ms, with a catcher drawn onto a switch
 * of it by o, the player waiting at its end up to WAIT_MS; -1 if none is. */
double lured(Situation &s, const Option &o, int k, double moveMs)
{
    const BotView &view = s.view;
    const std::vector<Timed> &sched = o.schedule;
    auto playerAt = [&](double clock) {
        Step at = sched.front().cell;
        for (const Timed &m : sched)
            if (view.now + m.t <= clock + 1e-6)
                at = m.cell;
        return at;
    };
    s.foes.load(view);
    while (s.foes.now() < view.now + o.cost + WAIT_MS) {
        const double prev = s.foes.now();
        s.foes.runTick(playerAt);
        if (s.foes.now() == prev)
            return -1;
        for (const FoeModel::Switched &w : s.foes.switched())
            if (w.byFoe && (int)w.bridge == k)
                return std::max(w.ms + moveMs, view.now + o.cost);
    }
    return -1;
}

/* The cells in line beyond a sticky pad not spent from a catcher beside it:
 * the player there lets the catcher onto the pad. */
std::vector<Step> releases(const BotView &view)
{
    static const int DU[] = {0, 1, 0, -1}, DV[] = {-1, 0, 1, 0};
    std::vector<Step> out;
    for (const FoeView &f : view.foes) {
        if (f.kind != 2 || f.dying)
            continue;
        for (int d = 0; d < 4; d++) {
            const int su = f.u + DU[d], sv = f.v + DV[d];
            const int ru = su + DU[d], rv = sv + DV[d];
            // kind 2: TILE_STICKY (tile.h)
            if (view.inside(su, sv) && view.inside(ru, rv) && view.cell(su, sv).kind == 2 &&
                !view.cell(su, sv).busy)
                out.push_back({ru, rv});
        }
    }
    return out;
}

/* The routes from at, where o ends, and the nav model they are over (with
 * the falling tiles o arms). */
struct After {
    After(const Situation &s, const Option &o, Step at) : nav(armed(s, o)),
        paths(nav, at.u, at.v, facingAfter(s.view.player.facing, o.script), o.cost) {}
    After(const After &) = delete;

    static NavModel armed(const Situation &s, const Option &o)
    {
        NavModel nav = s.nav;
        for (const Timed &t : o.schedule)
            nav.arm(t.cell, t.t);
        return nav;
    }

    NavModel nav;
    Paths    paths;
};

/* o walked on to target by paths, from where it ends. */
bool walkOn(const Situation &s, const Paths &paths, Step target, Option &o)
{
    Option leg;
    if (!walkTo(s, paths, target, 0, leg))
        return false;
    o.script.insert(o.script.end(), leg.script.begin(), leg.script.end());
    o.schedule.insert(o.schedule.end(), leg.schedule.begin() + 1, leg.schedule.end());
    o.cost = leg.cost;
    return true;
}

class LureAction : public Action {
public:
    const char *name() const override { return "lure"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        bool catchers = false;
        for (const FoeView &foe : board.live().foes)
            catchers |= foe.kind == 2 && !foe.dying;
        if (!catchers)
            return;
        for (const auto &[cell, k] : board.switches()) {
            const BridgeView &b = board.live().bridges[k];
            if (b.moving || board.extended(f.changes, k))
                continue;
            const Changes next = f.changes ^ board.worlds().bridgeBit(k);
            const Step here{f.at.u, f.at.v};
            if (!board.opens(next, here, f.changes, f.at))
                continue;
            Successor n{f, {}};
            n.facts.changes = next;
            n.step.end   = {cell.u, cell.v, 0};   // the switch: the player stays about
            n.step.ms    = LURE_MS + b.moveMs() + SETTLE_MS;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlayerView &p = view.player;
        if (p.falling)
            return;
        const Place &end = plan.steps.front().end;
        const int k = board.switched({end.u, end.v});
        if (k < 0)
            return;
        const double moveMs = view.bridges[k].moveMs() + SETTLE_MS;
        // Tried already as things stand, and none found.
        std::vector<int> state{k, p.u, p.v, p.facing};
        for (const FoeView &f : view.foes)
            state.insert(state.end(), {f.u, f.v, f.facing, f.moving, f.held, f.dying});
        for (const BridgeView &b : view.bridges)
            state.insert(state.end(), {b.extended, b.moving});
        const auto tried = failed_.find(state);
        if (tried != failed_.end() && view.now - tried->second < RETRY_MS && view.now >= tried->second)
            return;

        std::vector<signed char> back(view.cells.size(), -1);
        const std::vector<Step> first = nearest(s, s.from, CELLS, back);
        Option best;
        bool found = false;
        auto consider = [&](Option &&o, Step cell, const char *how) {
            const double done = lured(s, o, k, moveMs);
            if (done < 0 || (found && done - view.now >= best.stepMs))
                return;
            o.script.push_back({ScriptStep::Wait, 0, {}, done});
            o.schedule.push_back({done - view.now, cell});
            if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
                return;
            o.checked = true;
            o.stepMs  = done - view.now;
            o.cost    = plan.cost - plan.steps.front().ms + o.stepMs;
            char buf[128];
            snprintf(buf, sizeof(buf), "lure a catcher onto (%d,%d) for bridge %d, from (%d,%d)%s",
                     end.u, end.v, view.bridges[k].slot, cell.u, cell.v, how);
            o.what = buf;
            best = std::move(o);
            found = true;
        };
        {
            Option o;
            o.schedule.push_back({0, {p.u, p.v}});
            o.cost = s.from.startMs();
            consider(std::move(o), {p.u, p.v}, "");
        }
        for (const Step &cell : first) {
            Option o;
            if (walkTo(s, s.from, cell, 0, o))
                consider(std::move(o), cell, "");
        }
        // Across a cell in line beyond a sticky pad from a catcher, which
        // lets the catcher off the pad (foemodel.cpp's stepLegal), then on to
        // a cell to wait on.
        for (const Step &r : releases(view)) {
            for (const Step &a : first) {
                if (abs(a.u - r.u) + abs(a.v - r.v) != 1)
                    continue;
                Option going;
                if (!walkTo(s, s.from, a, 0, going))
                    continue;
                const After on(s, going, a);
                for (const Step &b : nearest(s, on.paths, SECONDS, back)) {
                    Option crossing = going;
                    if (!walkOn(s, on.paths, b, crossing))
                        continue;
                    bool across = false;
                    for (size_t i = going.schedule.size(); i < crossing.schedule.size(); i++)
                        across |= crossing.schedule[i].cell.u == r.u && crossing.schedule[i].cell.v == r.v;
                    if (!across)
                        continue;
                    char how[48];
                    snprintf(how, sizeof(how), " across (%d,%d)", r.u, r.v);
                    consider(Option(crossing), b, how);
                    const After then(s, crossing, b);
                    for (const Step &c : nearest(s, then.paths, SECONDS, back)) {
                        Option o = crossing;
                        if (walkOn(s, then.paths, c, o))
                            consider(std::move(o), c, how);
                    }
                }
            }
        }
        if (found) {
            out.push_back(std::move(best));
        } else {
            if (failed_.size() >= FAILED)
                failed_.clear();
            failed_[state] = view.now;
        }
    }

private:
    mutable std::map<std::vector<int>, double> failed_;   // when, the clock's ms
};

}  // namespace

const Action &lureAction()
{
    static const LureAction action;
    return action;
}

}  // namespace bot
