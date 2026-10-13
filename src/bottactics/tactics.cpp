#include "tactics.h"
#include <algorithm>
#include <stdio.h>
#include "danger.h"
#include "drops.h"
#include "walk.h"

namespace bot {

namespace {

/* How many other gems, or pickups, besides the planned one are offered,
 * nearest first. */
const size_t OTHERS = 6;
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

/* The planned target first, costed with the rest of the plan after it, then
 * the nearest others of the same kind, costed with the whole plan still to
 * do. */
std::vector<std::pair<Step, double>> targets(const Situation &s, const Plan &plan, Step planned,
                                             const std::vector<Step> &others)
{
    auto timeTo = [&](Step c) { return s.from.cost(c.u, c.v); };
    std::vector<std::pair<Step, double>> out{{planned, plan.cost - timeTo(planned)}};
    std::vector<std::pair<double, Step>> near;
    for (const Step &c : others)
        if ((c.u != planned.u || c.v != planned.v) && timeTo(c) >= 0)
            near.push_back({timeTo(c), c});
    std::sort(near.begin(), near.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < near.size() && i < OTHERS; i++)
        out.push_back({near[i].second, plan.cost});
    return out;
}

}  // namespace

Situation::Situation(const BotView &v, FoeModel &m)
    : view(v), nav(v), from(nav, v.player.u, v.player.v, v.player.facing), foes(m)
{
}

bool realise(Situation &s, const Board &board, const Plan &plan, Option *out, Tally *tally)
{
    if (plan.steps.empty())
        return false;
    const PlanStep &first = plan.steps.front();
    std::vector<Option> options;
    const Step end{first.end.u, first.end.v};
    switch (first.kind) {
    case COLLECT:
        offerWalks(s, targets(s, plan, first.gems.front(), board.gems(0)), "gem", true, options);
        break;
    case BOMBS:
    {
        std::vector<Step> live;
        for (const Pickup &p : board.pickups())
            if (p.block < 0)
                live.push_back(p.cell);
        offerWalks(s, targets(s, plan, end, live), "bombs", true, options);
    }
        break;
    case EXIT:
        offerWalks(s, targets(s, plan, end, {}), "exit", true, options);
        break;
    case BLAST:
        offerBlast(s, first.end, options);
        break;
    case AMBUSH:
        offerAmbushes(s, options);
        break;
    default:
        break;
    }
    return pick(s, options, out, tally);
}

bool approach(Situation &s, Step goal, Option *out, Tally *tally)
{
    std::vector<Option> options;
    const double whole = s.from.cost(goal.u, goal.v);
    for (const Step &c : s.from.cellsTo(goal.u, goal.v)) {
        const double soFar = s.from.cost(c.u, c.v);
        if (soFar <= 0 || (c.u == goal.u && c.v == goal.v))
            continue;  // the start, a cell passed only by a forced move, or the goal itself
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

bool evade(Situation &s, Option *out, Tally *tally)
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
    for (size_t i = 0; i < cells.size() && i < EVADES; i++)
        to.push_back({cells[i].second, 0});
    std::vector<Option> options;
    offerWalks(s, to, "evade to", false, options);
    return pick(s, options, out, tally);
}

}  // namespace bot
