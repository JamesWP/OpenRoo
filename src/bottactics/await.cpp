/* Await: with a follower out after the catchers (board.h, hunted), a foe
 * less, by keeping out of the way while it bombs them.  Its cost is a guess:
 * when the follower comes up with a catcher only the foe model says.  Done
 * as a walk to a cell near the player, or none, and a wait there, taken only
 * where the player stays safe through the wait (danger.h), the follower's
 * bombs included; the waits the foe model sees a catcher die in first. */
#include "action.h"
#include <algorithm>
#include <stdio.h>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes an await to cost. */
const double AWAIT_MS = 2000;
/* The wait, and the cells within this many ms, the nearest few. */
const double WAIT_MS  = 3000;
const double NEAR_MS  = 2500;
const size_t CELLS    = 12;
/* How long the cell must stay safe after the wait. */
const double LINGER_MS = 3000;

class AwaitAction : public Action {
public:
    const char *name() const override { return "await"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (board.hunted() <= 0 || f.foes <= f.carriers)
            return;
        Successor n{f, {}};
        n.facts.foes--;
        n.step.end = f.at;
        n.step.ms  = AWAIT_MS;
        out.push_back(n);
    }

    void offer(Situation &s, const Board &, const Plan &, std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlayerView &p = view.player;
        if (p.falling)
            return;
        Near cells{{0, {p.u, p.v}}};
        const Near near = nearCells(view, s.from, NEAR_MS, CELLS);
        cells.insert(cells.end(), near.begin(), near.end());
        for (const auto &[walkMs, at] : cells) {
            Option o;
            if (!walkTo(s, s.from, at, 0, o))
                continue;
            const double until = o.cost + WAIT_MS;
            waitTill(view, o, until);
            o.lingerMs = LINGER_MS;
            const Danger d = checkPlan(s.foes, view, o.schedule, o.lingerMs);
            if (d.hit)
                continue;
            o.checked = true;
            o.stepMs  = until;
            // A wait the follower kills in is the one to take.
            o.cost = until + (d.foeKills > 0 ? 0 : WAIT_MS);
            char buf[64];
            snprintf(buf, sizeof(buf), "await at (%d,%d), %d killed", at.u, at.v, d.foeKills);
            o.what = buf;
            out.push_back(std::move(o));
        }
    }
};

}  // namespace

const Action &awaitAction()
{
    static const AwaitAction action;
    return action;
}

}  // namespace bot
