/* Switch(k): a bridge switched, by coming to rest on a switch of its slot,
 * and waited for until its move is done (terrain.h): extended, where that
 * leads somewhere new from the switch, or the switch itself does (passed over
 * on the way somewhere new), or another switch it leads to does; or retracted, if it is not in the way or the
 * switch leads somewhere new (to pass over a switch).  Not the switch the player stands on (it
 * works only when stepped onto), nor a bridge on the move (the game ignores
 * the switch then).  A bridge that lets catchers in to the player costs
 * more than its time.  Done as a walk to the planned switch and a wait there. */
#include <stdio.h>
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

/* The waits a walk to a switch is also offered after, ms. */
const double WAITS[] = {0, 600, 1500, 3000};
/* The wait past the move's end, for the last deck cell to be stamped. */
const double SETTLE_MS = 100;
/* What letting a catcher in to the player costs: more than a way round a
 * switch, less than a bridge the level needs. */
const double LET_IN_MS = 5000;

/* Whether a switch other than cell, that cell can reach in world next,
 * reaches more in next than in s. */
bool beyond(const Board &board, Changes s, Changes next, Step cell)
{
    for (const auto &[other, k] : board.switches())
        if ((other.u != cell.u || other.v != cell.v) &&
            board.time(next, {cell.u, cell.v, 0}, other) >= 0 &&
            board.reach(next, other) > board.reach(s, other))
            return true;
    return false;
}

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
            const double t = board.time(f.changes, f.at, cell);
            if (t < 0)
                continue;
            // Retracting the way, but for coming to a switch that leads
            // somewhere new (one passed over).
            if (board.extended(f.changes, k) && (wanted & bit) != 0 &&
                !board.opens(next, cell, f.changes, f.at))
                continue;
            // Extending a bridge is worth it only if it leads somewhere new,
            // or the switch does, passed over (one of a row of switches), or
            // another switch does (the way it opens is beyond that one).
            if (!board.extended(f.changes, k) && board.reach(next, cell) <= board.reach(f.changes, cell) &&
                !board.opens(next, cell, f.changes, f.at) && !beyond(board, f.changes, next, cell))
                continue;
            Successor n{f, {}};
            n.facts.changes = next;
            n.facts.at   = {cell.u, cell.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = cell;
            n.step.ms    = t + b.moveMs() + SETTLE_MS;
            n.step.penalty = LET_IN_MS * board.letsIn(next, cell, f.changes, f.at);
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
        // Now, or after a wait: a foe working the switches may have the
        // bridges on the way as they should be only for a while.
        for (const double wait : WAITS) {
            Option o;
            if (!walkTo(s, s.from, cell, wait, o))
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
    }
};

}  // namespace

const Action &switchAction()
{
    static const SwitchAction action;
    return action;
}

}  // namespace bot
