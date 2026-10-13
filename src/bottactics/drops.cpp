#include "drops.h"
#include <algorithm>
#include <stdio.h>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* The cells to walk away to: within this many ms of the drop, the nearest
 * RETREATS of them. */
const double RETREAT_MS = 2500;
const size_t RETREATS = 16;
/* A blast's wait ends this long after its zone clears. */
const double CLEAR_MARGIN_MS = 200;

/* The ways to walk clear after a drop at site facing facing, dropIn ms from
 * now: the option so far (its script and schedule up to the drop) with a
 * retreat added and checked.  wait: stay until the blast has cleared. */
void retreats(Situation &s, const Option &before, Step site, int facing, double dropIn,
              bool wait, int needKills, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const Blast blast = predictBomb(view, site.u, site.v, view.cell(site.u, site.v).height,
                                    facing, view.now + dropIn);
    if (blast.lost)
        return;
    const Paths away(s.nav, site.u, site.v, facing, dropIn);
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = away.cost(u, v);
            if (c > 0 && c <= RETREAT_MS)
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < cells.size() && i < RETREATS; i++) {
        const Step at = cells[i].second;
        Option o = before;
        o.script.push_back({ScriptStep::Drop, 0, {}, 0});
        Option walk;
        if (!walkTo(s, away, at, 0, dropIn, walk))
            continue;
        o.script.insert(o.script.end(), walk.script.begin(), walk.script.end());
        o.schedule.insert(o.schedule.end(), walk.schedule.begin() + 1, walk.schedule.end());
        o.cost = walk.cost;
        o.blasts.push_back(blast);
        if (wait)
            o.script.push_back({ScriptStep::Wait, 0, {}, blast.until + CLEAR_MARGIN_MS});
        // The end must stay safe until the blast is over.
        o.lingerMs = std::max(1000.0, blast.until - view.now - o.schedule.back().t + 200);
        const Danger d = checkPlan(s.foes, view, o.schedule, o.lingerMs, o.blasts);
        if (d.hit || d.killed < needKills)
            continue;
        o.checked = true;
        o.kills = d.killed;
        o.what = before.what;
        char buf[96];
        snprintf(buf, sizeof(buf), ", away to (%d,%d)", at.u, at.v);
        o.what += buf;
        out.push_back(std::move(o));
    }
}

}  // namespace

void offerAmbushes(Situation &s, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const PlayerView &p = view.player;
    if (p.bombs <= 0 || view.foes.empty())
        return;
    // Turn to each facing (none, right, left, about), drop, walk away.
    static const int TURNS[4][2] = {{0, 0}, {1, 0}, {-1, 0}, {1, 1}};
    for (const auto &turn : TURNS) {
        const int nturns = (turn[0] != 0) + (turn[1] != 0);
        int facing = p.facing;
        for (int i = 0; i < nturns; i++)
            facing = NavModel::turned(facing, turn[i]);
        const double dropIn = std::max(nturns * p.stepMs, p.bombReadyAt - view.now) + view.tickMs;
        Option before;
        char buf[64];
        snprintf(buf, sizeof(buf), "ambush: bomb facing %d", facing);
        before.what = buf;
        if (nturns > 0)
            before.script.push_back({ScriptStep::Turn, facing, {}, 0});
        before.schedule.push_back({0, {p.u, p.v}});
        const size_t first = out.size();
        retreats(s, before, {p.u, p.v}, facing, dropIn, false, 1, out);
        for (size_t i = first; i < out.size(); i++) {
            char more[48];
            snprintf(more, sizeof(more), ", %d foe%s", out[i].kills, out[i].kills == 1 ? "" : "s");
            out[i].what += more;
        }
    }
}

void offerBlast(Situation &s, Place site, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const PlayerView &p = view.player;
    Option before;
    char buf[64];
    snprintf(buf, sizeof(buf), "blast from (%d,%d) facing %d", site.u, site.v, site.facing);
    before.what = buf;
    // There first, then the turn.
    if (!walkTo(s, s.from, {site.u, site.v}, 0, 0, before))
        return;
    const int arrives = s.from.facingAt(site.u, site.v);
    const int turns = (site.facing - arrives + 4) % 4;
    const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
    if (turns > 0)
        before.script.push_back({ScriptStep::Turn, site.facing, {}, 0});
    const double dropIn = std::max(before.cost + turnMs, p.bombReadyAt - view.now) + view.tickMs;
    before.schedule.push_back({dropIn, {site.u, site.v}});
    retreats(s, before, {site.u, site.v}, site.facing, dropIn, true, 0, out);
}

}  // namespace bot
