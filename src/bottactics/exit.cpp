/* Exit: with no gems needed, and no bombs taken and left unused, to the
 * exit.  Done as a walk there. */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class ExitAction : public Action {
public:
    const char *name() const override { return "exit"; }

    void expand(const Facts &f, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.gemsNeeded > 0 || f.unused)
            return;
        const double t = board.time(f.changes, f.at, board.exit());
        if (t < 0)
            return;
        Successor n{f, {}};
        n.facts.atExit = true;
        n.facts.at     = {board.exit().u, board.exit().v, 0};
        n.step.end   = n.facts.at;
        n.step.heads = board.exit();
        n.step.ms    = t;
        out.push_back(n);
    }

    void offer(Situation &s, const Board &, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const Place &end = plan.steps.front().end;
        offerWalks(s, targets(s, plan, {end.u, end.v}, {}), "exit", true, out);
    }
};

}  // namespace

const Action &exitAction()
{
    static const ExitAction action;
    return action;
}

}  // namespace bot
