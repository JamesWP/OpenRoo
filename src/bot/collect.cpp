#include "collect.h"
#include <stdio.h>
#include "logger.h"
#include "nav.h"
#include "route.h"
#include "tile.h"

namespace bot {

namespace {

/* What the nav model reads, hashed: the routes stand while it holds. */
unsigned long long terrain_hash(const BotView &view)
{
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&h](unsigned x) { h = (h ^ x) * 1099511628211ull; };
    for (const Cell &c : view.cells) {
        mix(c.height);
        mix(c.kind);
        mix(c.busy);
        mix(c.slideDir);
    }
    mix((unsigned)view.player.stepMs);
    return h;
}

}  // namespace

void Collect::survey(const BotView &view)
{
    const unsigned long long t = terrain_hash(view);
    if (surveyed_ && t == terrain_)
        return;
    surveyed_ = true;
    terrain_  = t;
    gemCells_.clear();
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++)
            if (view.cell(u, v).contents == CONTENTS_CRYSTAL)
                gemCells_.push_back({u, v});
    const NavModel nav(view);
    const size_t n = gemCells_.size();
    between_.assign(n, std::vector<double>(n + 1, -1));
    for (size_t i = 0; i < n; i++) {
        // Leaving a gem, the facing is whichever it was reached with; the
        // player's now stands in for it, a turn or two off at most.
        const Paths fromGem(nav, gemCells_[i].u, gemCells_[i].v, view.player.facing);
        for (size_t j = 0; j < n; j++)
            between_[i][j] = i == j ? 0 : fromGem.cost(gemCells_[j].u, gemCells_[j].v);
        between_[i][n] = fromGem.cost(view.exitU, view.exitV);
    }
}

void Collect::plan(const BotView &view)
{
    planned_ = true;
    noRoute_ = false;
    gems_ = view.player.gems;
    const PlayerView &p = view.player;
    const NavModel nav(view);
    const Paths from(nav, p.u, p.v, p.facing);
    survey(view);

    int target_u = view.exitU, target_v = view.exitV;
    const int k = view.gemsRequired - p.gems;
    char buf[160];
    if (k > 0) {
        // The gems still on the map that the player can reach.
        std::vector<int> live;
        for (size_t i = 0; i < gemCells_.size(); i++) {
            const Step &g = gemCells_[i];
            if (view.cell(g.u, g.v).contents == CONTENTS_CRYSTAL && from.cost(g.u, g.v) >= 0)
                live.push_back((int)i);
        }
        const int n = (int)live.size();
        const size_t exitCol = gemCells_.size();
        std::vector<std::vector<double>> dist(n + 2, std::vector<double>(n + 2, -1));
        for (int i = 0; i < n; i++) {
            dist[0][i + 1] = from.cost(gemCells_[live[i]].u, gemCells_[live[i]].v);
            for (int j = 0; j < n; j++)
                dist[i + 1][j + 1] = between_[live[i]][live[j]];
            dist[i + 1][n + 1] = between_[live[i]][exitCol];
        }
        dist[0][n + 1] = from.cost(view.exitU, view.exitV);
        double cost;
        const std::vector<int> order = planRoute(dist, k, &cost);
        if (order.empty()) {
            snprintf(buf, sizeof(buf), "no route: %d gems needed, %d reachable, exit %s",
                     k, n, dist[0][n + 1] >= 0 ? "reachable" : "unreachable");
            note_ = buf;
            g_logger.write("bot: collect: %s\n", buf);
            noRoute_ = true;
            motor_.follow({});
            return;
        }
        snprintf(buf, sizeof(buf), "route: %d of %d gems, %.0f ms", k, n, cost);
        note_ = buf;
        target_u = gemCells_[live[order[0] - 1]].u;
        target_v = gemCells_[live[order[0] - 1]].v;
    } else {
        noRoute_ = from.cost(view.exitU, view.exitV) < 0;
        note_ = noRoute_ ? "no route: exit unreachable" : "to the exit";
    }
    std::vector<Step> path = from.cellsTo(target_u, target_v);
    motor_.follow(std::move(path));
}

unsigned Collect::act(const BotView &view)
{
    if (view.player.atRest()) {
        // Again when the gems or the terrain change, or the motor is done
        // with its path; not every tick while there is nothing to do.
        const bool stalled = motor_.status() == Motor::Idle || motor_.status() == Motor::Arrived ||
                             motor_.status() == Motor::Blocked;
        if (!planned_ || view.player.gems != gems_ ||
            (stalled && (!noRoute_ || terrain_hash(view) != terrain_)))
            plan(view);
    }
    return motor_.act(view);
}

}  // namespace bot
