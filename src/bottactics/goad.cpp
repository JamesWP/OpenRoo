/* Goad: with a thrower about that bombs the player and a carrier, the carrier
 * killed by the thrower's bomb, no bomb of the player's: a foe less and the
 * carrier's gem (board.h) taken, if gems are needed.  Its cost is a guess:
 * whether the bomb finds the carrier only the foe model says.  Done as a walk
 * to a cell near the carrier, or one near the player for a carrier that
 * follows it, and a wait there, taken only where the foe model
 * has a thrower's bomb kill a foe and the player stays safe throughout
 * (danger.h). */
#include "action.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include "danger.h"
#include "rules.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a goad to cost: the thrower's coming, its bomb and
 * the gem picked up. */
const double GOAD_MS = 15000;
/* The cells to wait on: within NEAR cells of a carrier and WALK_MS of the
 * player, the nearest CELLS of them, with these waits there. */
const int    NEAR = 3;
const double WALK_MS = 6000;
const size_t CELLS = 8;
const double WAITS[] = {0, 2000, 5000};
/* A carrier that follows the player may be led through the bomb instead:
 * the cells within LEAD_MS of the player, the nearest LEADS of them. */
const double LEAD_MS = 2000;
const size_t LEADS = 12;
/* How long the player waits for the bomb, at most. */
const double LINGER_MS = 8000;

/* FoeView::kind of a thrower; FoeView::type of a follower, which bombs only
 * catchers (foetype.h). */
const int THROWER  = 3;
const int FOLLOWER = 5;

/* A thrower that bombs the player. */
bool thrower(const BotView &view)
{
    for (const FoeView &f : view.foes)
        if (f.kind == THROWER && f.type != FOLLOWER && !f.held && !f.dying)
            return true;
    return false;
}

/* Whether the throwers, where the facts put them, can walk to within a
 * bomb's reach (NEAR_BOMB) of a carrier. */
const int NEAR_BOMB = 2;
bool throwable(const Facts &f, const Board &board)
{
    const BotView &view = board.live();
    const std::vector<char> &walk = board.throwerWalk(f.throwerAt);
    if (walk.empty())
        return false;
    for (const FoeView &foe : view.foes) {
        if (!Board::carrier(foe) || foe.dying)
            continue;
        for (int v = foe.v - NEAR_BOMB; v <= foe.v + NEAR_BOMB; v++)
            for (int u = foe.u - NEAR_BOMB; u <= foe.u + NEAR_BOMB; u++)
                if (view.inside(u, v) && walk[(size_t)v * view.extentU + u])
                    return true;
    }
    return false;
}

class GoadAction : public Action {
public:
    const char *name() const override { return "goad"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.carriers <= 0 || !thrower(board.live()) || !throwable(f, board))
            return;
        // The player stands by a carrier, for the thrower following to bomb:
        // none walled in where the facts leave the blocks.
        const BotView &view = board.live();
        Step at{-1, -1};
        for (const FoeView &foe : view.foes) {
            if (!Board::carrier(foe) || foe.dying)
                continue;
            for (int v = foe.v - 1; v <= foe.v + 1 && at.u < 0; v++)
                for (int u = foe.u - 1; u <= foe.u + 1 && at.u < 0; u++)
                    if (view.inside(u, v) && board.time(f.changes, f.at, {u, v}) >= 0)
                        at = {u, v};
        }
        if (at.u < 0)
            return;
        Successor n{f, {}};
        n.facts.carriers--;
        n.facts.foes--;
        n.facts.unused = false;
        if (n.facts.gemsNeeded > 0)
            n.facts.gemsNeeded--;
        n.step.ms    = GOAD_MS;
        n.step.heads = at;
        out.push_back(n);
    }

    void offer(Situation &s, const Board &, const Plan &, std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        if (view.player.falling || !thrower(view))
            return;
        std::vector<Step> carriers;
        for (const FoeView &foe : view.foes)
            if (Board::carrier(foe) && !foe.dying)
                carriers.push_back({foe.u, foe.v});
        if (carriers.empty())
            return;
        const Step here{view.player.u, view.player.v};
        std::vector<std::pair<double, Step>> cells{{0, here}}, leads;
        for (int v = 0; v < view.extentV; v++)
            for (int u = 0; u < view.extentU; u++) {
                const double c = s.from.cost(u, v) - s.from.startMs();
                if (c <= 0 || c > WALK_MS || NavModel::moving(view.cell(u, v)) || switchedBy(view, u, v))
                    continue;
                bool near = false;
                for (const Step &k : carriers)
                    near = near || chebyshev(k, {u, v}) <= NEAR;
                if (near)
                    cells.push_back({c, {u, v}});
                else if (c <= LEAD_MS)
                    leads.push_back({c, {u, v}});
            }
        auto nearest = [](auto &list, size_t first, size_t most) {
            std::stable_sort(list.begin() + first, list.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            if (list.size() > first + most)
                list.resize(first + most);
        };
        nearest(cells, 1, CELLS);
        nearest(leads, 0, LEADS);
        cells.insert(cells.end(), leads.begin(), leads.end());
        for (const auto &[ms, at] : cells)
            for (const double wait : WAITS) {
                Option o;
                if (!walkTo(s, s.from, at, 0, o))
                    break;
                waitTill(view, o, o.cost + wait);
                o.lingerMs = LINGER_MS;
                const Danger d = checkPlan(s.foes, view, o.schedule, o.lingerMs);
                if (d.hit || d.foeKills <= 0)
                    continue;
                o.checked = true;
                waitTill(view, o, o.cost + LINGER_MS);
                o.stepMs  = o.cost;
                char buf[80];
                snprintf(buf, sizeof(buf), "goad a thrower's bomb onto a carrier, waiting at (%d,%d)",
                         at.u, at.v);
                o.what = buf;
                out.push_back(std::move(o));
                return;
            }
    }
};

}  // namespace

const Action &goadAction()
{
    static const GoadAction action;
    return action;
}

}  // namespace bot
