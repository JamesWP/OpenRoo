/* Lure(k): bridge k switched by a foe, the player drawing it there: for a
 * switch the player cannot work and walk away from (one it cannot come to,
 * or one it is stuck on after, as on a jump pad's perch), where the bridge
 * leads somewhere new.  Its cost is a guess: when a foe comes to the switch
 * only the foe model says.  Done as a wait on a cell near the player, or
 * laps to and from one, as many as it takes the foe model to have a foe work
 * the switch; taken only where the player stays safe throughout (danger.h). */
#include "action.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <stdio.h>
#include <stdlib.h>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a lure to cost. */
const double LURE_MS = 15000;
/* The cells to lure from: within this many ms, the nearest CELLS; and as
 * many within PIVOT_MS of the player and FOE_NEAR cells of a foe at rest
 * there, to wake it; each paired with the nearest CELLS within NEAR_MS of
 * it, to draw it off. */
const double NEAR_MS  = 1500;
const size_t CELLS    = 8;
const double PIVOT_MS = 6000;
const int    FOE_NEAR = 2;
/* The longest a lure goes on for, and the wait offered on a cell. */
const double HORIZON_MS = 30000;
/* How long the end must stay safe. */
const double LINGER_MS = 2000;
/* The wait past the bridge's move, for the last deck cell to be stamped. */
const double SETTLE_MS = 100;

/* The lures found to be none, for a state the foe model would play out the
 * same from: the player and the foes at rest, no falling tile armed (no clock
 * running but the bridges'), so a state seen again, as the player waits
 * about, is not tried again.  Its key, or 0 where it is no such state. */
unsigned long long stillKey(const BotView &view, size_t k)
{
    const PlayerView &p = view.player;
    if (!p.atRest())
        return 0;
    for (const FallingView &t : view.fallings)
        if (t.armed)
            return 0;
    unsigned long long h = navHash(view) ^ 0x9e3779b97f4a7c15ull;
    auto mix = [&h](long long x) { h = (h ^ (unsigned long long)x) * 1099511628211ull; };
    mix((long long)k);
    mix(p.u);
    mix(p.v);
    mix(p.facing);
    for (const FoeView &f : view.foes) {
        if (!f.atRest())
            return 0;
        for (const long long x : {(long long)f.u, (long long)f.v, (long long)f.h, (long long)f.facing,
                                  (long long)f.kind, (long long)f.type, (long long)f.lastU,
                                  (long long)f.lastV, (long long)f.turned, (long long)f.held})
            mix(x);
    }
    return h ? h : 1;
}

/* Whether the player can work the switch and walk on from it. */
bool workable(const Board &board, Changes s, Place at, Step cell, Changes next)
{
    return board.time(s, at, cell) >= 0 && board.reach(next, cell) > 1;
}

/* When the foe model has a foe set bridge k going, the player keeping to
 * plan and standing at its end: ms from now, or -1.  Only an odd number of
 * goes leave it switched. */
double lured(Situation &s, const std::vector<Timed> &plan, double untilMs, size_t k)
{
    const BotView &view = s.view;
    follow(s.foes, view, plan, view.now + untilMs);
    double first = -1;
    int goes = 0;
    for (const FoeModel::Switched &w : s.foes.switched())
        if (w.bridge == k) {
            goes++;
            if (w.byFoe && first < 0)
                first = w.ms - view.now;
        }
    return goes % 2 == 1 ? first : -1;
}

/* FoeView::type of a switch runner (foetype.h). */
const int SWITCH_RUNNER = 2;

/* Whether a foe might ever work bridge k's switch: one of its switches is
 * raised (a switch runner goes only to those, FoeModel::nearestSwitch), or a
 * foe that is no switch runner walks about. */
bool lurable(const Board &board, size_t k)
{
    const BotView &view = board.live();
    for (const FoeView &f : view.foes)
        if (f.type != SWITCH_RUNNER && f.kind != 3 && !f.dying)
            return true;
    for (const auto &[cell, b] : board.switches())
        if ((size_t)b == k && view.cell(cell.u, cell.v).busy)
            return true;
    return false;
}

class LureAction : public Action {
public:
    const char *name() const override { return "lure"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        if (f.foes <= 0)
            return;
        for (const auto &[cell, k] : board.switches()) {
            const BridgeView &b = board.live().bridges[k];
            if (b.moving || board.extended(f.changes, k) || !lurable(board, k))
                continue;
            const Changes next = f.changes ^ board.worlds().bridgeBit(k);
            if (workable(board, f.changes, f.at, cell, next) ||
                !board.opens(next, {f.at.u, f.at.v}, f.changes, f.at))
                continue;
            if (!board.worlds().has(next) && board.worlds().size() > options.maxWorlds)
                continue;
            Successor n{f, {}};
            n.facts.changes = next;
            n.step.end = f.at;
            n.step.arg = k;
            n.step.ms  = LURE_MS + b.moveMs();
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
        const int arg = plan.steps.front().arg;
        if (arg < 0 || (size_t)arg >= view.bridges.size() || view.bridges[arg].moving)
            return;
        const size_t k = (size_t)arg;
        const double moveMs = view.bridges[k].moveMs() + SETTLE_MS;
        static std::unordered_set<unsigned long long> none;
        const unsigned long long key = stillKey(view, k);
        if (key && none.count(key))
            return;
        const size_t before = out.size();
        struct Remember {
            std::unordered_set<unsigned long long> &none;
            unsigned long long key;
            const std::vector<Option> &out;
            size_t before;
            ~Remember()
            {
                if (key && out.size() == before)
                    none.insert(key);
            }
        } remember{none, key, out, before};

        const Step here{p.u, p.v};
        // The nearest CELLS a walk from paths ends on within withinMs, where
        // keep says.
        auto nearest = [&](const Paths &paths, double withinMs, const std::function<bool(Step)> &keep) {
            std::vector<std::pair<double, Step>> cells;
            for (int v = 0; v < view.extentV; v++)
                for (int u = 0; u < view.extentU; u++) {
                    const double c = paths.cost(u, v) - paths.startMs();
                    if (c > 0 && c <= withinMs && !NavModel::moving(view.cell(u, v)) &&
                        !switchedBy(view, u, v) && keep({u, v}))
                        cells.push_back({c, {u, v}});
                }
            std::stable_sort(cells.begin(), cells.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            if (cells.size() > CELLS)
                cells.resize(CELLS);
            return cells;
        };
        const auto any = [](Step) { return true; };
        const std::vector<std::pair<double, Step>> near = nearest(s.from, NEAR_MS, any);

        auto take = [&](Option o, double at, const char *how, Step c) {
            const double done = at + moveMs;
            if (done > o.cost + 1)
                o.script.push_back({ScriptStep::Wait, 0, {}, view.now + done});
            o.lingerMs = std::max(LINGER_MS, done - o.schedule.back().t + LINGER_MS);
            if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
                return false;
            o.checked = true;
            o.stepMs  = done;
            o.cost    = done;
            char buf[80];
            snprintf(buf, sizeof(buf), "lure a foe to bridge %zu, %s (%d,%d)", k, how, c.u, c.v);
            o.what = buf;
            out.push_back(std::move(o));
            return true;
        };

        // A wait, here or on a cell near.
        std::vector<std::pair<double, Step>> cells{{0, here}};
        cells.insert(cells.end(), near.begin(), near.end());
        for (const auto &[walkMs, at] : cells) {
            Option o;
            if (walkMs > 0) {
                if (!walkTo(s, s.from, at, 0, o))
                    continue;
            } else {
                o.schedule.push_back({0, at});
                o.cost = s.from.startMs();
            }
            const double t = lured(s, o.schedule, o.cost + HORIZON_MS, k);
            if (t >= 0 && take(o, std::max(t, o.cost), "waiting at", at))
                return;
        }

        // Rounds: from here to a cell and on between it and another, again
        // and again, cut at the leg a foe works the switch in.  The cells:
        // near here, back and forth from here; and near a foe, back and forth
        // with each cell near that.
        std::vector<std::vector<Step>> rounds;
        for (const auto &[ms, a] : near)
            rounds.push_back({a, here});
        std::vector<std::pair<double, Step>> pivots = near;
        for (const auto &c : nearest(s.from, PIVOT_MS, [&](Step c) {
                 for (const FoeView &f : view.foes)
                     if (f.atRest() && !f.dying && abs(f.u - c.u) <= FOE_NEAR && abs(f.v - c.v) <= FOE_NEAR)
                         return true;
                 return false;
             }))
            pivots.push_back(c);
        for (const auto &[ms, a] : pivots) {
            const Paths fromA(s.nav, a.u, a.v, 1, 0, Bound{NEAR_MS});
            for (const auto &[ms2, b] : nearest(fromA, NEAR_MS, any))
                rounds.push_back({a, b});
        }
        for (const std::vector<Step> &round : rounds) {
            Option o;
            o.schedule.push_back({0, here});
            o.cost = s.from.startMs();
            // Where each leg ends: the script's and the schedule's sizes, and the time.
            struct End {
                size_t script, schedule;
                double ms;
            };
            std::vector<End> ends;
            Step at = here;
            for (size_t legs = 0; o.cost < HORIZON_MS && legs < 96; legs++) {
                const Step target = round[legs % round.size()];
                if (target.u == at.u && target.v == at.v)
                    continue;
                if (!walkOn(s, s.nav, o, at, target))
                    break;
                ends.push_back({o.script.size(), o.schedule.size(), o.cost});
                at = target;
            }
            if (ends.empty())
                continue;
            const double t = lured(s, o.schedule, o.cost, k);
            if (t < 0)
                continue;
            // Cut at the end of the leg it happens in.
            for (const End &e : ends)
                if (e.ms >= t) {
                    o.script.resize(e.script);
                    o.schedule.resize(e.schedule);
                    o.cost = e.ms;
                    break;
                }
            char how[48];
            snprintf(how, sizeof(how), "to and fro (%d,%d) and", round[1].u, round[1].v);
            if (take(o, std::max(t, o.cost), how, round.front()))
                return;
        }

    }
};

}  // namespace

const Action &lureAction()
{
    static const LureAction action;
    return action;
}

}  // namespace bot
