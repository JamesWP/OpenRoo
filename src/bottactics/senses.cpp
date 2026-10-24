#include "senses.h"
#include <algorithm>
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
    worlds.update(view);
    const Board board(view, worlds);
    PlanOptions options;
    options.goal = FINISH;
    const Plan finish = planGoal(board, planActions(), options);
    std::vector<Timed> way{{0, {view.player.u, view.player.v}}};
    double crossBy = finish.found ? 0 : -1;
    for (const PlanStep &step : finish.steps) {
        if (std::string(step.name) == "cross")
            break;
        const Step from = way.back().cell, to{step.end.u, step.end.v};
        const Paths walk(kept, from.u, from.v, view.player.facing);
        if (walk.cost(to.u, to.v) >= 0)
            for (const Step &c : walk.cellsTo(to.u, to.v))
                way.push_back({crossBy + walk.cost(c.u, c.v), c});
        crossBy += step.ms;
        way.push_back({crossBy, to});
    }
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
    const double until = view.now + std::min(crossBy, WRECK_HORIZON_MS);
    while (foes.now() + view.tickMs <= until + 1e-6 && view.tickMs > 0) {
        foes.runTick([&](double clock) {
            Step at = way.front().cell;
            for (const Timed &m : way)
                if (view.now + m.t <= clock + 1e-6)
                    at = m.cell;
            return at;
        });
        for (size_t i = 0; i < foes.foeCount(); i++) {
            const Step c = foes.foeCell(i);
            if (i < onRun.size() && view.inside(c.u, c.v) && runs[(size_t)c.v * view.extentU + c.u] >= 0)
                onRun[i] = 1;
        }
    }
    for (const char r : onRun)
        n += r;
    return n;
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
    }
    senses.farm = gemsFromSpawners(view);
    return selectGoals(senses);
}

}  // namespace bot
