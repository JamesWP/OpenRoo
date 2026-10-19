/* Exit: with no gems needed, and no bombs taken and left unused, to the
 * exit (against the clock, by way of the time bonuses it needs to be in
 * time).  Done as a walk there, or to the next bonus. */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class ExitAction : public Action {
public:
    const char *name() const override { return "exit"; }

    void expand(const Facts &f, double left, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        if (f.gemsNeeded > 0 || f.unused)
            return;
        Successor n{f, {}};
        n.facts.atExit = true;
        n.facts.at     = {board.exit().u, board.exit().v, 0};
        n.step.end   = n.facts.at;
        n.step.heads = board.exit();
        if (options.timed) {
            const GemRoute &r = board.timedRoute(f.changes, f.at, 0, f.taken, f.bonuses, left);
            if (r.found) {
                n.facts.bonuses |= r.bonuses;
                if (!r.gems.empty())
                    n.step.heads = r.gems.front();
                n.step.ms = r.toLast + r.toExit;
                out.push_back(n);
                return;
            }
        }
        n.step.ms = board.time(f.changes, f.at, board.exit());
        if (n.step.ms >= 0)
            out.push_back(n);
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const Step heads = plan.steps.front().heads;
        offerWalks(s, targets(s, plan, heads, {}), board.timeBonus(heads) ? "time" : "exit", true,
                   out);
    }
};

}  // namespace

const Action &exitAction()
{
    static const ExitAction action;
    return action;
}

}  // namespace bot
