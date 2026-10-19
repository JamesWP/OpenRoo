#include "walk.h"
#include <stdio.h>
#include <algorithm>

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

bool walkTo(const Situation &s, const Paths &paths, Step target, double wait, double startMs,
            Option &o)
{
    std::vector<Timed> plan = paths.scheduleTo(target.u, target.v);
    if (plan.empty())
        return false;
    // The lifts keep their own time: a wait before setting off would move
    // the whole route against them.
    if (wait > 0 && s.nav.timed())
        return false;
    o.schedule.clear();
    o.schedule.push_back({0, plan.front().cell});
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
        if (legs[i].waitUntil > 0)
            o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + legs[i].waitUntil});
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

void offerWalks(const Situation &s, const std::vector<std::pair<Step, double>> &targets,
                const char *what, bool waits, std::vector<Option> &out)
{
    for (const double wait : WAITS) {
        if (wait > 0 && !waits)
            break;
        for (const auto &[t, after] : targets) {
            Option o;
            if (!walkTo(s, s.from, t, wait, 0, o))
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
