#include "moves.h"
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include "danger.h"
#include "playout.h"
#include "walk.h"

namespace bot {

std::vector<Near> nearCells(const Situation &s, const Paths &paths, double withinMs, size_t most,
                            const std::function<bool(Step)> &keep, bool switches)
{
    const BotView &view = s.view;
    std::vector<Near> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = paths.cost(u, v);
            if (c <= 0 || c > withinMs || NavModel::moving(view.cell(u, v)) ||
                (!switches && switchedBy(view, u, v)) || (keep && !keep({u, v})))
                continue;
            cells.push_back({c, {u, v}});
        }
    std::stable_sort(cells.begin(), cells.end(), [](const Near &a, const Near &b) { return a.ms < b.ms; });
    if (cells.size() > most)
        cells.resize(most);
    return cells;
}

bool goTo(const Situation &s, Step at, Option &o)
{
    if (at.u == s.view.player.u && at.v == s.view.player.v) {
        o.schedule.push_back({0, at});
        o.cost = s.from.startMs();
        return true;
    }
    return walkTo(s, s.from, at, 0, o);
}

bool waitForFoes(Situation &s, Option &o, Step at, double maxMs,
                 const std::function<bool(FoeModel &)> &came)
{
    const BotView &view = s.view;
    if (view.tickMs <= 0)
        return false;
    const std::function<Step(double)> player = keepingTo(view, o.schedule);
    s.foes.load(view);
    const double arrive = view.now + o.cost;
    while (s.foes.now() < arrive + maxMs) {
        const double prev = s.foes.now();
        s.foes.runTick(player);
        if (s.foes.now() == prev)
            return false;
        if (s.foes.now() >= arrive && came(s.foes)) {
            if (s.foes.now() - view.now > o.cost + 1)
                waitTill(view, o, s.foes.now() - view.now, at);
            return true;
        }
    }
    return false;
}

bool commit(Situation &s, Option &o, double stepMs, double lingerMs, std::string what)
{
    o.lingerMs = lingerMs;
    if (checkPlan(s.foes, s.view, o.schedule, o.lingerMs).hit)
        return false;
    o.checked = true;
    o.stepMs  = stepMs;
    o.cost    = stepMs;
    o.what    = std::move(what);
    return true;
}

std::string describe(const char *format, ...)
{
    char buf[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

}  // namespace bot
