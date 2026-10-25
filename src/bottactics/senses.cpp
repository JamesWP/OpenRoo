#include "senses.h"
#include <algorithm>
#include <functional>
#include <string>
#include "action.h"
#include "board.h"
#include "danger.h"
#include "goap.h"
#include "nav.h"

namespace bot {

namespace {

/* The longest the foes are followed for, to the crossing (wreckers). */
const double WRECK_HORIZON_MS = 60000;
/* FoeView::kind of a catcher. */
const int CATCHER = 2;
/* The longest they are followed for along the way to finish (switchers). */
const double SWITCH_HORIZON_MS = 30000;

/* Where the player is over the way to finish, keeping to the plan and
 * walking from each step's end to the next on the ground as the steps before
 * have switched it, up to a step called stop: the places, and *ms the time
 * it reaches (-1: no plan). */
std::vector<Timed> finishWay(const BotView &view, Worlds &worlds, const char *stop, double *ms)
{
    worlds.update(view);
    const Board board(view, worlds);
    PlanOptions options;
    options.goal = FINISH;
    const Plan finish = planGoal(board, planActions(), options);
    std::vector<Timed> way{{0, {view.player.u, view.player.v}}};
    *ms = finish.found ? 0 : -1;
    Changes changes = 0;
    for (const PlanStep &step : finish.steps) {
        if (stop && std::string(step.name) == stop)
            break;
        // A switch (the one it ends on) or a lure switches its bridge; a drop
        // crosses it out and leaves it in.
        const Step from = way.back().cell, to{step.end.u, step.end.v};
        const std::string name = step.name;
        const int k = name == "switch" ? board.switched(to) : step.arg;
        const bool bridge = k >= 0 && (size_t)k < view.bridges.size();
        if (bridge && name == "drop" && !board.extended(changes, (size_t)k))
            changes ^= worlds.bridgeBit((size_t)k);
        const Paths walk(worlds.get(changes).nav(), from.u, from.v, view.player.facing);
        if (walk.cost(to.u, to.v) >= 0)
            for (const Step &c : walk.cellsTo(to.u, to.v))
                way.push_back({*ms + walk.cost(c.u, c.v), c});
        *ms += step.ms;
        way.push_back({*ms, to});
        if (bridge && (name == "switch" || name == "lure" || name == "drop"))
            changes ^= worlds.bridgeBit((size_t)k);
    }
    return way;
}

/* The foes, loaded, run in the foe model until untilMs, the player keeping
 * to way; each after every tick. */
void follow(FoeModel &foes, const BotView &view, const std::vector<Timed> &way, double untilMs,
            const std::function<void()> &each)
{
    while (foes.now() + view.tickMs <= untilMs + 1e-6 && view.tickMs > 0) {
        foes.runTick(keeping(way, view.now));
        each();
    }
}

/* The foes that would walk onto a run of falling tiles that do not come
 * back (nav.h, keptOffRuns) before the way to finish crosses it, where the
 * exit is out of reach but over such a run; and the cell beside such a run
 * the player gets to soonest.  A foe follows the player (foemodel.h): it is
 * run in the foe model, the player keeping to the way to finish, walking
 * from each step's end to the next. */
int wreckers(const BotView &view, FoeModel &foes, Worlds &worlds, Step *guard)
{
    const std::vector<int> runs = keptOffRuns(view);
    const NavModel kept(view), over(view, true);
    const Paths plain(kept, view.player.u, view.player.v, view.player.facing);
    const Paths crossing(over, view.player.u, view.player.v, view.player.facing);
    if (plain.cost(view.exitU, view.exitV) >= 0 || crossing.cost(view.exitU, view.exitV) < 0)
        return 0;
    double best = -1;
    for (size_t i = 0; i < runs.size(); i++) {
        if (runs[i] < 0)
            continue;
        const int u = (int)i % view.extentU, v = (int)i / view.extentU;
        const int du[] = {1, -1, 0, 0}, dv[] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            const int bu = u + du[d], bv = v + dv[d];
            const double t = view.inside(bu, bv) ? plain.cost(bu, bv) : -1;
            if (t >= 0 && (best < 0 || t < best)) {
                best = t;
                *guard = {bu, bv};
            }
        }
    }
    // When the way to finish crosses, and where the player is till then.
    double crossBy;
    const std::vector<Timed> way = finishWay(view, worlds, "cross", &crossBy);
    int n = 0;
    if (crossBy < 0) {
        // No way to finish: any foe that gets to such a run.
        for (const FoeView &f : view.foes) {
            if (f.dying)
                continue;
            const Paths walks(over, f.u, f.v, f.facing);
            bool gets = false;
            for (size_t i = 0; i < runs.size() && !gets; i++)
                gets = runs[i] >= 0 && walks.cost((int)i % view.extentU, (int)i / view.extentU) >= 0;
            n += gets;
        }
        return n;
    }
    foes.load(view);
    std::vector<char> onRun(foes.foeCount(), 0);
    follow(foes, view, way, view.now + std::min(crossBy, WRECK_HORIZON_MS), [&] {
        for (size_t i = 0; i < foes.foeCount(); i++) {
            const Step c = foes.foeCell(i);
            if (i < onRun.size() && view.inside(c.u, c.v) && runs[(size_t)c.v * view.extentU + c.u] >= 0)
                onRun[i] = 1;
        }
    });
    for (const char r : onRun)
        n += r;
    return n;
}

/* Whether a catcher would work a switch while the player goes the way to
 * finish that takes a deck from under the player (foemodel.h): switched back
 * behind the player, a bridge the plan relies on is lost. */
bool switchers(const BotView &view, FoeModel &foes, Worlds &worlds)
{
    // Only a catcher follows the player onto a deck to be dropped off it
    // (drop.cpp); and the way to finish is planned for nothing else.
    if (view.bridges.empty() ||
        std::none_of(view.foes.begin(), view.foes.end(),
                     [](const FoeView &f) { return f.kind == CATCHER && !f.dying && !f.held; }))
        return false;
    double ms;
    const std::vector<Timed> way = finishWay(view, worlds, nullptr, &ms);
    if (ms < 0)
        return false;
    foes.load(view);
    bool lost = false;
    follow(foes, view, way, view.now + std::min(ms, SWITCH_HORIZON_MS), [&] {
        if (lost || std::none_of(foes.switched().begin(), foes.switched().end(),
                                 [](const FoeModel::Switched &w) { return w.byFoe; }))
            return;
        Step at = way.front().cell;
        for (const Timed &m : way)
            if (view.now + m.t <= foes.now() + 1e-6)
                at = m.cell;
        lost = foes.deckGone(at.u, at.v);
    });
    return lost;
}

}  // namespace

std::vector<Goal> senseGoals(const BotView &view, FoeModel &foes, Worlds &worlds,
                             const std::vector<Timed> &ahead)
{
    Senses senses;
    senses.bombs = view.player.bombs;
    senses.foes  = (int)view.foes.size();
    if (!view.foes.empty()) {
        senses.foeMeetsMs = foeMeets(foes, view, ahead, FOE_HORIZON_MS);
        senses.hunted     = catchersHunted(foes, view);
        senses.wreckers   = wreckers(view, foes, worlds, &senses.guard);
        senses.switchers  = switchers(view, foes, worlds);
    }
    senses.farm = gemsFromSpawners(view);
    return selectGoals(senses);
}

}  // namespace bot
