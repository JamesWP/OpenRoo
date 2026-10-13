#include "walk.h"
#include <stdio.h>

namespace bot {

namespace {

/* The waits a walk is also offered after, ms. */
const double WAITS[] = {0, 600, 1500, 3000};

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

}  // namespace bot
