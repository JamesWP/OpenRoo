#include "collect.h"
#include <stdio.h>
#include <algorithm>
#include "danger.h"
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

/* Waits tried before setting off, ms; and how long the player must be able
 * to stand where a way ends. */
const double WAITS[] = {0, 600, 1500, 3000};
const double LINGER_MS = 1000;
/* How many gems, in order of cost, are tried when the route's first is not
 * safe to go for. */
const size_t OTHER_GEMS = 6;
/* Cells within this many ms tried as places to step away to. */
const double REFUGE_MS = 1500;
/* How many of the nearest of those are tried, each without a wait: each try
 * runs the foes forward. */
const size_t REFUGES = 12;
/* Ticks between checks of the way being walked, and between looks for a way
 * out when there is no safe move. */
const unsigned long CHECK_TICKS = 15;

std::vector<Timed> delayed(const std::vector<Timed> &plan, double wait)
{
    std::vector<Timed> out;
    out.reserve(plan.size() + 1);
    if (plan.empty())
        return out;
    out.push_back({0, plan.front().cell});
    for (const Timed &m : plan)
        out.push_back({m.t + wait, m.cell});
    return out;
}

}  // namespace

bool Collect::goSafely(const BotView &view, const Paths &from, const std::vector<Step> &targets,
                       const char *what, bool waits)
{
    for (const double wait : WAITS) {
        if (wait > 0 && !waits)
            break;
        for (const Step &t : targets) {
            const std::vector<Timed> plan = delayed(from.scheduleTo(t.u, t.v), wait);
            if (plan.empty())
                continue;
            const Danger d = checkPlan(sim_, view, plan, LINGER_MS);
            if (d.hit)
                continue;
            char buf[160];
            snprintf(buf, sizeof(buf), "%s (%d,%d)%s", what, t.u, t.v,
                     wait > 0 ? ", after a wait" : "");
            note_ = buf;
            target_ = t;
            motor_.follow(from.cellsTo(t.u, t.v), view.now + wait);
            return true;
        }
    }
    return false;
}

bool Collect::stillSafe(const BotView &view)
{
    if (target_.u < 0 || motor_.status() != Motor::Walking)
        return true;
    const PlayerView &p = view.player;
    const NavModel nav(view);
    const Paths from(nav, p.u, p.v, p.facing);
    std::vector<Timed> plan = from.scheduleTo(target_.u, target_.v);
    if (plan.empty())
        return true;
    // A step under way ends before the next can start.
    const double left = p.moving ? std::max(0.0, p.animStart + p.animMs - view.now) : 0;
    for (size_t i = 1; i < plan.size(); i++)
        plan[i].t += left;
    return !checkPlan(sim_, view, plan, LINGER_MS).hit;
}

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
        // The route's first gem, then the others nearest first.
        std::vector<Step> targets{gemCells_[live[order[0] - 1]]};
        std::vector<std::pair<double, int>> rest;
        for (int i = 0; i < n; i++)
            if (i + 1 != order[0])
                rest.push_back({dist[0][i + 1], live[i]});
        std::sort(rest.begin(), rest.end());
        for (size_t i = 0; i < rest.size() && i < OTHER_GEMS; i++)
            targets.push_back(gemCells_[rest[i].second]);
        if (goSafely(view, from, targets, "gem"))
            return;
    } else {
        noRoute_ = from.cost(view.exitU, view.exitV) < 0;
        if (noRoute_) {
            note_ = "no route: exit unreachable";
            motor_.follow({});
            return;
        }
        if (goSafely(view, from, {{view.exitU, view.exitV}}, "exit"))
            return;
    }

    // Nowhere to go safely: step away to somewhere to stand.
    std::vector<std::pair<double, Step>> refuges;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = from.cost(u, v);
            if (c > 0 && c <= REFUGE_MS)
                refuges.push_back({c, {u, v}});
        }
    std::sort(refuges.begin(), refuges.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<Step> cells;
    for (size_t i = 0; i < refuges.size() && i < REFUGES; i++)
        cells.push_back(refuges[i].second);
    if (goSafely(view, from, cells, "refuge", false))
        return;
    note_ = "no safe move";
    target_ = {-1, -1};
    motor_.follow({});
}

unsigned Collect::act(const BotView &view)
{
    // The way being walked is checked again as the foes move.
    if (++ticks_ % CHECK_TICKS == 0 && !view.foes.empty() && !stillSafe(view))
        motor_.halt();
    if (view.player.atRest()) {
        // Again when the gems or the terrain change, or the motor is done
        // with its path; not every tick while there is nothing to do.
        const bool stalled = motor_.status() == Motor::Idle || motor_.status() == Motor::Arrived ||
                             motor_.status() == Motor::Blocked;
        // With no safe move, look again only every few ticks: each look
        // runs the foes forward for every way it tries.
        const bool cornered = target_.u < 0 && planned_ && !noRoute_;
        if (!planned_ || view.player.gems != gems_ ||
            (stalled && !noRoute_ && (!cornered || ticks_ % CHECK_TICKS == 0)) ||
            (stalled && noRoute_ && terrain_hash(view) != terrain_))
            plan(view);
    }
    return motor_.act(view);
}

}  // namespace bot
