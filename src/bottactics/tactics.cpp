#include "tactics.h"
#include <algorithm>
#include <stdio.h>
#include "danger.h"
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

/* Cells within this many ms are offered to evade to, the nearest EVADES. */
const double EVADE_MS = 1500;
const size_t EVADES = 12;

/* The cheapest safe option; options are sorted. */
bool pick(Situation &s, std::vector<Option> &options, Option *out, Tally *tally)
{
    std::stable_sort(options.begin(), options.end(),
                     [](const Option &a, const Option &b) { return a.cost < b.cost; });
    bool found = false;
    for (Option &o : options) {
        tally->offered++;
        if (!o.checked && checkPlan(s.foes, s.view, o.schedule, o.lingerMs, o.blasts).hit)
            continue;
        tally->safe++;
        if (!found) {
            *out  = std::move(o);
            found = true;
        }
        if (!tally->all)
            break;
    }
    return found;
}

}  // namespace

namespace {

/* How long the player is busy yet: the move under way, and the fall into
 * the cell it is stepping to, if that is lower than the player is. */
double busy(const BotView &v)
{
    const PlayerView &p = v.player;
    if (!p.moving)
        return 0;
    double ms = std::max(0.0, p.animStart + p.animMs - v.now);
    // Stairs (kinds 5 to 8) are no drop.
    const Cell &to = v.cell(p.u, p.v);
    if (!(to.kind >= 5 && to.kind <= 8))
        ms += NavModel::fallMs(p.h - to.height);
    return ms;
}

}  // namespace

Situation::Situation(const BotView &v, FoeModel &m)
    : view(v), nav(v), busyMs(busy(v)),
      from(nav, v.player.u, v.player.v, v.player.facing, busyMs), foes(m)
{
}

bool realise(Situation &s, const Board &board, const Plan &plan, Option *out, Tally *tally)
{
    if (plan.steps.empty())
        return false;
    std::vector<Option> options;
    actions()[plan.steps.front().action]->offer(s, board, plan, options);
    return pick(s, options, out, tally);
}

bool approach(Situation &s, const Board &board, Step goal, Option *out, Tally *tally)
{
    std::vector<Option> options;
    const double whole = s.from.cost(goal.u, goal.v);
    for (const Step &c : s.from.cellsTo(goal.u, goal.v)) {
        const double soFar = s.from.cost(c.u, c.v);
        if (soFar <= 0 || (c.u == goal.u && c.v == goal.v) || !board.canFinishFrom(c))
            continue;  // the start, a cell passed only by a forced move, the goal itself, a trap
        Option o;
        if (!walkTo(s, s.from, c, 0, 0, o))
            continue;
        o.cost = whole - soFar;
        char buf[96];
        snprintf(buf, sizeof(buf), "approach (%d,%d) towards (%d,%d)", c.u, c.v, goal.u, goal.v);
        o.what = buf;
        options.push_back(std::move(o));
    }
    return pick(s, options, out, tally);
}

bool evade(Situation &s, const Board &board, Option *out, Tally *tally)
{
    const BotView &view = s.view;
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = s.from.cost(u, v);
            if (c > 0 && c <= EVADE_MS)
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::pair<Step, double>> to;
    for (size_t i = 0; i < cells.size() && to.size() < EVADES; i++)
        if (board.canFinishFrom(cells[i].second))
            to.push_back({cells[i].second, 0});
    std::vector<Option> options;
    offerWalks(s, to, "evade to", false, options);
    return pick(s, options, out, tally);
}

}  // namespace bot
