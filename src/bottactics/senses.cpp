#include "senses.h"
#include <algorithm>
#include <functional>
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
 * have left it (or, if there is no way there, as the step leaves it), up to
 * the first crossing of a one-shot run if toCrossing: the places, and *ms
 * the time it reaches (-1: no plan). */
std::vector<Timed> finishWay(const BotView &view, Worlds &worlds, bool toCrossing, double *ms)
{
    worlds.update(view);
    const Board board(view, worlds);
    PlanOptions options;
    options.goal = FINISH;
    const Plan finish = planGoal(board, planActions(), options);
    std::vector<Timed> way{{0, {view.player.u, view.player.v}}};
    *ms = finish.found ? 0 : -1;
    Facts before;
    for (const PlanStep &step : finish.steps) {
        if (toCrossing && step.after.crossed != before.crossed)
            break;
        const Step from = way.back().cell, to{step.end.u, step.end.v};
        for (const Changes s : {before.changes, step.after.changes}) {
            const Paths walk(worlds.get(s).nav(), from.u, from.v, view.player.facing);
            if (walk.cost(to.u, to.v) < 0)
                continue;
            for (const Step &c : walk.cellsTo(to.u, to.v))
                way.push_back({*ms + walk.cost(c.u, c.v), c});
            break;
        }
        *ms += step.ms;
        way.push_back({*ms, to});
        before = step.after;
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
    const std::vector<Timed> way = finishWay(view, worlds, true, &crossBy);
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
    const std::vector<Timed> way = finishWay(view, worlds, false, &ms);
    if (ms < 0)
        return false;
    foes.load(view);
    bool lost = false;
    follow(foes, view, way, view.now + std::min(ms, SWITCH_HORIZON_MS), [&] {
        if (lost || std::none_of(foes.switched().begin(), foes.switched().end(),
                                 [](const FoeModel::Switched &w) { return w.byFoe; }))
            return;
        const Step at = entryAt(way, view.now, foes.now()).cell;
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
