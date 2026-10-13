#include "actions.h"
#include <algorithm>
#include <stdio.h>
#include "danger.h"
#include "route.h"
#include "tile.h"

namespace bot {

namespace {

/* Waits a progress option is also offered after, ms. */
const double WAITS[] = {0, 600, 1500, 3000};
/* How many gems besides the route's next are offered, nearest first. */
const size_t OTHER_GEMS = 6;
/* Cells within this many ms are offered as refuges, the nearest REFUGES. */
const double REFUGE_MS = 1500;
const size_t REFUGES = 12;
/* An ambush's retreat: cells within this many ms of the drop, the nearest
 * RETREATS of them. */
const double RETREAT_MS = 2500;
const size_t RETREATS = 16;

/* A walk to (u, v) from paths, after a wait, as an option's script and
 * schedule. */
bool walk_to(const PlanContext &ctx, const Paths &paths, Step target, double wait, double startMs,
             Option &o)
{
    std::vector<Timed> plan = paths.scheduleTo(target.u, target.v);
    if (plan.empty())
        return false;
    o.schedule.clear();
    o.schedule.push_back({0, plan.front().cell});
    for (const Timed &m : plan)
        o.schedule.push_back({m.t + startMs + wait, m.cell});
    if (wait > 0)
        o.script.push_back({ScriptStep::Wait, 0, {}, ctx.view.now + startMs + wait});
    ScriptStep w;
    w.kind = ScriptStep::Walk;
    w.path = paths.cellsTo(target.u, target.v);
    o.script.push_back(w);
    o.cost = startMs + wait + paths.cost(target.u, target.v);
    return true;
}

void offer_walks(PlanContext &ctx, const std::vector<std::pair<Step, double>> &targets, int tier,
                 const char *what, bool waits, std::vector<Option> &out)
{
    for (const double wait : WAITS) {
        if (wait > 0 && !waits)
            break;
        for (const auto &[t, after] : targets) {
            Option o;
            o.tier = tier;
            if (!walk_to(ctx, ctx.from, t, wait, 0, o))
                continue;
            o.cost += after;
            char buf[96];
            snprintf(buf, sizeof(buf), "%s (%d,%d)%s", what, t.u, t.v, wait > 0 ? " after a wait" : "");
            o.what = buf;
            out.push_back(std::move(o));
        }
    }
}

}  // namespace

unsigned long long terrainHash(const BotView &view)
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

PlanContext::PlanContext(const BotView &v, FoeModel &m)
    : view(v), nav(v), from(nav, v.player.u, v.player.v, v.player.facing), foes(m)
{
}

void GemSurvey::update(const BotView &view)
{
    const unsigned long long t = terrainHash(view);
    if (done_ && t == terrain_)
        return;
    done_ = true;
    terrain_ = t;
    gems_.clear();
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++)
            if (view.cell(u, v).contents == CONTENTS_CRYSTAL)
                gems_.push_back({u, v});
    const NavModel nav(view);
    const size_t n = gems_.size();
    between_.assign(n, std::vector<double>(n + 1, -1));
    for (size_t i = 0; i < n; i++) {
        // Leaving a gem, the facing is whichever it was reached with; the
        // player's now stands in for it, a turn or two off at most.
        const Paths from(nav, gems_[i].u, gems_[i].v, view.player.facing);
        for (size_t j = 0; j < n; j++)
            between_[i][j] = i == j ? 0 : from.cost(gems_[j].u, gems_[j].v);
        between_[i][n] = from.cost(view.exitU, view.exitV);
    }
}

void proposeProgress(PlanContext &ctx, GemSurvey &survey, std::vector<Option> &out)
{
    const BotView &view = ctx.view;
    const Paths &from = ctx.from;
    const int k = view.gemsRequired - view.player.gems;
    char buf[160];
    if (k <= 0) {
        if (from.cost(view.exitU, view.exitV) < 0) {
            ctx.stuck = "no route: exit unreachable";
            return;
        }
        ctx.goal = {view.exitU, view.exitV};
        offer_walks(ctx, {{ctx.goal, 0}}, PROGRESS, "exit", true, out);
        return;
    }
    survey.update(view);
    const std::vector<Step> &gems = survey.gems();
    std::vector<int> live;
    for (size_t i = 0; i < gems.size(); i++)
        if (view.cell(gems[i].u, gems[i].v).contents == CONTENTS_CRYSTAL &&
            from.cost(gems[i].u, gems[i].v) >= 0)
            live.push_back((int)i);
    const int n = (int)live.size();
    std::vector<std::vector<double>> dist(n + 2, std::vector<double>(n + 2, -1));
    for (int i = 0; i < n; i++) {
        dist[0][i + 1] = from.cost(gems[live[i]].u, gems[live[i]].v);
        for (int j = 0; j < n; j++)
            dist[i + 1][j + 1] = survey.between(live[i], live[j]);
        dist[i + 1][n + 1] = survey.toExit(live[i]);
    }
    dist[0][n + 1] = from.cost(view.exitU, view.exitV);
    double cost;
    const std::vector<int> order = planRoute(dist, k, &cost);
    if (order.empty()) {
        snprintf(buf, sizeof(buf), "no route: %d gems needed, %d reachable, exit %s", k, n,
                 dist[0][n + 1] >= 0 ? "reachable" : "unreachable");
        ctx.stuck = buf;
        return;
    }
    // The route's next gem, costed with the route after it; then the others
    // nearest first, costed with the whole route still to do.
    std::vector<std::pair<Step, double>> targets{
        {gems[live[order[0] - 1]], cost - dist[0][order[0]]}};
    std::vector<std::pair<double, int>> rest;
    for (int i = 0; i < n; i++)
        if (i + 1 != order[0])
            rest.push_back({dist[0][i + 1], live[i]});
    std::sort(rest.begin(), rest.end());
    for (size_t i = 0; i < rest.size() && i < OTHER_GEMS; i++)
        targets.push_back({gems[rest[i].second], cost});
    ctx.goal = targets.front().first;
    offer_walks(ctx, targets, PROGRESS, "gem", true, out);
}

void proposeAdvance(PlanContext &ctx, std::vector<Option> &out)
{
    if (ctx.goal.u < 0)
        return;
    const double whole = ctx.from.cost(ctx.goal.u, ctx.goal.v);
    for (const Step &c : ctx.from.cellsTo(ctx.goal.u, ctx.goal.v)) {
        const double so_far = ctx.from.cost(c.u, c.v);
        if (so_far <= 0 || (c.u == ctx.goal.u && c.v == ctx.goal.v))
            continue;  // the start, a cell passed only by a forced move, or the goal itself
        Option o;
        o.tier = ADVANCE;
        if (!walk_to(ctx, ctx.from, c, 0, 0, o))
            continue;
        o.cost = whole - so_far;
        char buf[96];
        snprintf(buf, sizeof(buf), "advance to (%d,%d) towards (%d,%d)", c.u, c.v, ctx.goal.u,
                 ctx.goal.v);
        o.what = buf;
        out.push_back(std::move(o));
    }
}

void proposeBombs(PlanContext &ctx, std::vector<Option> &out)
{
    const BotView &view = ctx.view;
    if (view.player.bombs > 0 || view.foes.empty())
        return;
    std::vector<std::pair<double, Step>> pickups;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++)
            if (view.cell(u, v).contents == CONTENTS_GRANT_09 && ctx.from.cost(u, v) >= 0)
                pickups.push_back({ctx.from.cost(u, v), {u, v}});
    std::sort(pickups.begin(), pickups.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::pair<Step, double>> targets;
    for (const auto &p : pickups)
        targets.push_back({p.second, 0});
    offer_walks(ctx, targets, BOMBS, "bombs", true, out);
}

void proposeAmbush(PlanContext &ctx, std::vector<Option> &out)
{
    const BotView &view = ctx.view;
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
        const Paths away(ctx.nav, p.u, p.v, facing);
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
            o.tier = AMBUSH;
            if (nturns > 0)
                o.script.push_back({ScriptStep::Turn, facing, {}, 0});
            o.script.push_back({ScriptStep::Drop, 0, {}, 0});
            if (!walk_to(ctx, away, at, 0, dropIn, o))
                continue;
            o.blasts.push_back(blast);
            // The end must stay safe until the blast is over.
            o.lingerMs = std::max(1000.0, blast.until - view.now - o.schedule.back().t + 200);
            const Danger d = checkPlan(ctx.foes, view, o.schedule, o.lingerMs, o.blasts);
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

void proposeRefuge(PlanContext &ctx, std::vector<Option> &out)
{
    const BotView &view = ctx.view;
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = ctx.from.cost(u, v);
            if (c > 0 && c <= REFUGE_MS)
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::pair<Step, double>> targets;
    for (size_t i = 0; i < cells.size() && i < REFUGES; i++)
        targets.push_back({cells[i].second, 0});
    offer_walks(ctx, targets, REFUGE, "refuge", false, out);
}

}  // namespace bot
