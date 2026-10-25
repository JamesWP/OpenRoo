#include "walk.h"
#include <stdio.h>
#include <algorithm>
#include <cmath>

namespace bot {

namespace {

/* The waits a walk is also offered after, ms. */
const double WAITS[] = {0, 600, 1500, 3000};
/* How many other targets besides the planned one are offered, nearest
 * first. */
const size_t OTHERS = 6;
/* A step off a platform is held this long either side of the moment it
 * stands over the cell (script.h). */
const double STEP_OFF_MS = 110;

}  // namespace

int facingAfter(int facing, const Script &script)
{
    for (const ScriptStep &k : script) {
        if (k.kind == ScriptStep::Turn)
            facing = k.facing;
        else if (k.kind == ScriptStep::Walk)
            facing = Motor::facingAfter(facing, k.path);
    }
    return facing;
}

bool walkTo(const Situation &s, const Paths &paths, Step target, double wait, Option &o)
{
    const double startMs = paths.startMs();
    std::vector<Timed> plan = paths.scheduleTo(target.u, target.v);
    if (plan.empty())
        return false;
    // The lifts keep their own time: a wait before setting off would move
    // the whole route against them.
    if (wait > 0 && s.nav.timed())
        return false;
    // A wait ends on a tick: the player moves on one.
    if (wait > 0 && s.view.tickMs > 0)
        wait = std::ceil((startMs + wait) / s.view.tickMs - 1e-6) * s.view.tickMs - startMs;
    o.schedule.clear();
    o.schedule.push_back({0, plan.front().cell});
    if (plan.size() == 1) {
        o.cost = startMs;
        waitTill(s.view, o, startMs + wait);
        return true;
    }
    for (const Timed &m : plan)
        o.schedule.push_back({m.t + startMs + wait, m.cell});
    if (wait > 0)
        o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + startMs + wait});
    // Each leg of the route is walked after the wait before it (for a lift).
    std::vector<Leg> legs = paths.legsTo(target.u, target.v);
    for (size_t i = 0; i < legs.size(); i++) {
        if (legs[i].stepOff) {
            // Off the platform as it passes the cell, then on from beside it.
            const std::vector<Step> &c = legs[i].cells;
            o.script.push_back({ScriptStep::Turn, legs[i].facing, {}, 0});
            o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + legs[i].waitUntil - STEP_OFF_MS});
            o.script.push_back({ScriptStep::StepOff, 0, {c[0], c[1]},
                                s.view.now + legs[i].waitUntil + STEP_OFF_MS});
            o.script.push_back({ScriptStep::Walk, 0, {c.begin() + 1, c.end()}, 0});
            continue;
        }
        if (legs[i].waitUntil > 0) {
            // The turn to the way on is made before the wait, as the route has it.
            if (legs[i].facing)
                o.script.push_back({ScriptStep::Turn, legs[i].facing, {}, 0});
            o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + legs[i].waitUntil});
        }
        ScriptStep w;
        w.kind = ScriptStep::Walk;
        if (i > 0)
            w.path.push_back(legs[i - 1].cells.back());
        w.path.insert(w.path.end(), legs[i].cells.begin(), legs[i].cells.end());
        o.script.push_back(w);
    }
    o.cost = startMs + wait + paths.cost(target.u, target.v);
    return true;
}

void waitTill(const BotView &view, Option &o, double ms)
{
    if (ms <= o.cost)
        return;
    o.script.push_back({ScriptStep::Wait, 0, {}, view.now + ms});
    o.schedule.push_back({ms, o.schedule.back().cell});
    o.cost = ms;
}

void append(Option &o, const Option &leg)
{
    o.script.insert(o.script.end(), leg.script.begin(), leg.script.end());
    o.schedule.insert(o.schedule.end(), leg.schedule.begin() + 1, leg.schedule.end());
    o.cost = leg.cost;
}

static NavModel armed(const NavModel &nav, const std::vector<Timed> &schedule)
{
    NavModel after = nav;
    for (const Timed &t : schedule)
        after.arm(t.cell, t.t);
    return after;
}

RoutesOn::RoutesOn(const NavModel &nav, const std::vector<Timed> &schedule, Step at, int facing,
                   double setOffMs, const Bound &bound)
    : nav_(armed(nav, schedule)), paths_(nav_, at.u, at.v, facing, setOffMs, bound)
{
}

bool walkOn(const Situation &s, const NavModel &nav, Option &o, Step at, Step target, double wait)
{
    const RoutesOn on(nav, o.schedule, at, facingAfter(s.view.player.facing, o.script), o.cost,
                      Bound{-1, -1, target});
    Option leg;
    if (!walkTo(s, on.paths(), target, wait, leg))
        return false;
    append(o, leg);
    return true;
}

Near nearCells(const BotView &view, const Paths &paths, double withinMs, size_t most,
               const std::function<bool(Step)> &keep)
{
    Near cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = paths.cost(u, v);
            if (c > 0 && c <= withinMs && !NavModel::moving(view.cell(u, v)) && (!keep || keep({u, v})))
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    if (cells.size() > most)
        cells.resize(most);
    return cells;
}

std::vector<Step> retreatCells(const BotView &view, const Paths &paths, size_t most)
{
    std::vector<Step> out;
    for (const auto &[ms, c] : nearCells(view, paths, RETREAT_MS, most))
        out.push_back(c);
    return out;
}

void offerWalks(const Situation &s, const std::vector<std::pair<Step, double>> &targets,
                const char *what, bool waits, std::vector<Option> &out)
{
    for (const double wait : WAITS) {
        if (wait > 0 && !waits)
            break;
        for (const auto &[t, after] : targets) {
            Option o;
            if (!walkTo(s, s.from, t, wait, o))
                continue;
            o.cost += after;
            char buf[96];
            snprintf(buf, sizeof(buf), "%s (%d,%d)%s", what, t.u, t.v, wait > 0 ? " after a wait" : "");
            o.what = buf;
            out.push_back(std::move(o));
        }
    }
}


std::vector<std::pair<Step, double>> targets(const Situation &s, const Plan &plan, Step planned,
                                             const std::vector<Step> &others,
                                             const Board *traps)
{
    auto timeTo = [&](Step c) { return s.from.cost(c.u, c.v); };
    std::vector<std::pair<Step, double>> out{{planned, plan.cost - timeTo(planned)}};
    // Against the clock, no other that would make the plan too late.
    const double left = traps ? traps->timeLeft(0) : -1;
    auto inTime = [&](Step c) { return left < 0 || timeTo(c) + plan.ms <= std::max(left, plan.ms); };
    std::vector<std::pair<double, Step>> near;
    for (const Step &c : others)
        if ((c.u != planned.u || c.v != planned.v) && timeTo(c) >= 0 &&
            (!traps || (traps->canFinishAfter(c) && inTime(c))))
            near.push_back({timeTo(c), c});
    std::sort(near.begin(), near.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < near.size() && i < OTHERS; i++)
        out.push_back({near[i].second, plan.cost});
    return out;
}


}  // namespace bot
