/* Cross(r): a run of the falling tiles that do not come back crossed, from a
 * cell beside it to another (board.h, crossings).  The other walks keep off
 * such a run (nav.h), so a plan that crosses one says when: after what can
 * only be had from this side of it.  Done as a walk to the cell beside the
 * run, then over it to the other. */
#include <stdio.h>
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class CrossAction : public Action {
public:
    const char *name() const override { return "cross"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        for (const Crossing &c : board.crossings()) {
            if (f.crossed & c.bit)
                continue;
            const double t = board.time(f.changes, f.at, c.from);
            if (t < 0 || board.time(f.changes, f.at, {c.to.u, c.to.v}) >= 0)
                continue;  // out of reach, or leading nowhere new
            Successor n{f, {}};
            n.facts.crossed |= c.bit;
            n.facts.at   = c.to;
            n.step.end   = c.to;
            n.step.heads = c.from;
            n.step.ms    = t + c.ms;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &, const Plan &plan, std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlanStep &step = plan.steps.front();
        const Step from = step.heads, to{step.end.u, step.end.v};
        Option o;
        int facing = view.player.facing;
        if (from.u != view.player.u || from.v != view.player.v) {
            if (!walkTo(s, s.from, from, 0, o))
                return;
            facing = Motor::facingAfter(facing, s.from.cellsTo(from.u, from.v));
        } else {
            o.schedule.push_back({0, from});
            o.cost = s.from.startMs();
        }
        // Over the run, which only this walk may cross.
        const NavModel over(view, true);
        const Paths on(over, from.u, from.v, facing, o.cost);
        Option leg;
        if (!walkTo(s, on, to, 0, leg))
            return;
        o.script.insert(o.script.end(), leg.script.begin(), leg.script.end());
        o.schedule.insert(o.schedule.end(), leg.schedule.begin() + 1, leg.schedule.end());
        o.stepMs = leg.cost;
        o.cost   = plan.cost - step.ms + o.stepMs;
        char buf[96];
        snprintf(buf, sizeof(buf), "cross from (%d,%d) to (%d,%d)", from.u, from.v, to.u, to.v);
        o.what = buf;
        out.push_back(std::move(o));
    }
};

}  // namespace

const Action &crossAction()
{
    static const CrossAction action;
    return action;
}

}  // namespace bot
