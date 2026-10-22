/* Freeze: a freeze pickup taken before setting off, which holds the foes for
 * the steps right after (foemodel.h).  The planner weighs time alone, so a
 * plan takes one only once the steps that go straight on are found unsafe
 * (tactics.h, choose); and only as its first step, where the hold is of use.
 * Done as a walk to the planned freeze, or another. */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class FreezeAction : public Action {
public:
    const char *name() const override { return "freeze"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        const Place start = board.start();
        if (f.foes <= 0 || f.at.u != start.u || f.at.v != start.v || f.changes != 0 ||
            f.taken != 0 || f.pickups != 0 || f.bonuses != 0 || f.crossed != 0)
            return;
        for (const Step &c : board.freezes()) {
            const double t = board.time(0, f.at, c);
            if (t <= 0)
                continue;
            Successor n{f, {}};
            n.facts.at   = {c.u, c.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = c;
            n.step.ms    = t;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan, std::vector<Option> &out) const override
    {
        const Place &end = plan.steps.front().end;
        offerWalks(s, targets(s, plan, {end.u, end.v}, board.freezes()), "freeze", false, out);
    }
};

}  // namespace

const Action &freezeAction()
{
    static const FreezeAction action;
    return action;
}

}  // namespace bot
