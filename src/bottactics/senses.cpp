#include "senses.h"
#include <string>
#include "action.h"
#include "board.h"
#include "danger.h"
#include "goap.h"
#include "nav.h"

namespace bot {

namespace {

/* The foes that would walk onto a run of falling tiles that do not come
 * back (nav.h, keptOffRuns) before the way to finish crosses it, where the
 * exit is out of reach but over such a run; and the cell beside such a run
 * the player gets to soonest. */
int wreckers(const BotView &view, Worlds &worlds, Step *guard)
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
    // When the way to finish crosses: never, if there is none.
    worlds.update(view);
    const Board board(view, worlds);
    PlanOptions options;
    options.goal = FINISH;
    const Plan finish = planGoal(board, planActions(), options);
    double crossBy = finish.found ? 0 : 1e300;
    for (const PlanStep &step : finish.steps) {
        crossBy += step.ms;
        if (std::string(step.name) == "cross")
            break;
    }
    int n = 0;
    for (const FoeView &f : view.foes) {
        if (f.dying)
            continue;
        const Paths walks(over, f.u, f.v, f.facing);
        bool first = false;
        for (size_t i = 0; i < runs.size() && !first; i++) {
            const double t = runs[i] >= 0 ? walks.cost((int)i % view.extentU, (int)i / view.extentU) : -1;
            first = t >= 0 && t / view.player.stepMs * CATCHER_STEP_MS < crossBy;
        }
        n += first;
    }
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
        senses.wreckers   = wreckers(view, worlds, &senses.guard);
    }
    senses.farm = gemsFromSpawners(view);
    return selectGoals(senses);
}

}  // namespace bot
