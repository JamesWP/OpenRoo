/* Switch(k): a bridge switched, by coming to rest on a switch of its slot,
 * and waited for until its move is done (terrain.h): extended, where that
 * leads somewhere new from the switch or the switch itself does (passed over
 * on the way somewhere new), or retracted, if it is not in the way (to pass
 * over a switch).  Not the switch the player stands on (it
 * works only when stepped onto), nor a bridge on the move (the game ignores
 * the switch then).  Done as a walk to the planned switch and a wait there. */
#include <stdio.h>
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

/* The wait past the move's end, for the last deck cell to be stamped. */
const double SETTLE_MS = 100;

class SwitchAction : public Action {
public:
    const char *name() const override { return "switch"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        const Changes wanted = board.inTheWay(f.changes, f.at, false);
        for (const auto &[cell, k] : board.switches()) {
            const BridgeView &b = board.live().bridges[k];
            if (b.moving || (cell.u == f.at.u && cell.v == f.at.v))
                continue;
            const Changes bit = board.worlds().bridgeBit(k);
            const Changes next = f.changes ^ bit;
            if (board.extended(f.changes, k) ? (wanted & bit) != 0 : false)
                continue;  // retracting the way
            const double t = board.time(f.changes, f.at, cell);
            if (t < 0)
                continue;
            // Extending a bridge is worth it only if it leads somewhere new,
            // or the switch does, passed over (one of a row of switches).
            if (!board.extended(f.changes, k) && board.reach(next, cell) <= board.reach(f.changes, cell) &&
                !board.opens(next, cell, f.changes, f.at))
                continue;
            Successor n{f, {}};
            n.facts.changes = next;
            n.facts.at   = {cell.u, cell.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = cell;
            n.step.ms    = t + b.moveMs() + SETTLE_MS;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const Place &end = plan.steps.front().end;
        const Step cell{end.u, end.v};
        const int k = board.switched(cell);
        if (k < 0)
            return;
        const double moveMs = board.live().bridges[k].moveMs() + SETTLE_MS;
        Option o;
        if (!walkTo(s, s.from, cell, 0, o))
            return;
        // On the switch until the bridge is done.
        const double arrive = o.cost;
        o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + arrive + moveMs});
        o.schedule.push_back({arrive + moveMs, cell});
        o.stepMs = arrive + moveMs;
        o.cost = plan.cost - plan.steps.front().ms + o.stepMs;
        char buf[96];
        snprintf(buf, sizeof(buf), "switch (%d,%d) for bridge %d", cell.u, cell.v,
                 board.live().bridges[k].slot);
        o.what = buf;
        out.push_back(std::move(o));
    }
};

}  // namespace

const Action &switchAction()
{
    static const SwitchAction action;
    return action;
}

}  // namespace bot
