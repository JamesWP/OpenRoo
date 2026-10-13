#include "ambush.h"
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
        const Blast blast = predictBomb(view, p.u, p.v, p.h, facing, view.now + dropIn);
        if (blast.lost)
            continue;
        const Paths away(s.nav, p.u, p.v, facing);
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
            Option o;
            if (nturns > 0)
                o.script.push_back({ScriptStep::Turn, facing, {}, 0});
            o.script.push_back({ScriptStep::Drop, 0, {}, 0});
            if (!walkTo(s, away, at, 0, dropIn, o))
                continue;
            o.blasts.push_back(blast);
            // The end must stay safe until the blast is over.
            o.lingerMs = std::max(1000.0, blast.until - view.now - o.schedule.back().t + 200);
            const Danger d = checkPlan(s.foes, view, o.schedule, o.lingerMs, o.blasts);
            if (d.hit || d.killed == 0)
                continue;
            o.checked = true;
            o.kills = d.killed;
            char buf[96];
            snprintf(buf, sizeof(buf), "ambush: bomb facing %d, away to (%d,%d), %d foe%s",
                     facing, at.u, at.v, d.killed, d.killed == 1 ? "" : "s");
            o.what = buf;
            out.push_back(std::move(o));
        }
    }
}

}  // namespace bot
