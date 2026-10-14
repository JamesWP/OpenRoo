/* Ambush: with a bomb and a foe, a foe less and a bomb less.  Its cost is a
 * guess: whether one can be had depends on where the foes go, which only the
 * tactical layer predicts.  Done as a turn, a drop and a walk away, taken
 * only where the foe model says the blast kills a foe (drops.h). */
#include "action.h"
#include "drops.h"

namespace bot {

namespace {

/* What an ambush takes: turning, the drop, the walk away and the fuse. */
const double AMBUSH_MS = 2500;

class AmbushAction : public Action {
public:
    const char *name() const override { return "ambush"; }

    void expand(const Facts &f, const Board &, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.bombs <= 0 || f.foes <= 0)
            return;
        Successor n{f, {}};
        n.facts.bombs--;
        n.facts.foes--;
        n.facts.unused = false;
        n.step.ms = AMBUSH_MS;
        out.push_back(n);
    }

    void offer(Situation &s, const Board &, const Plan &, std::vector<Option> &out) const override
    {
        offerAmbushes(s, out);
    }
};

}  // namespace

const Action &ambushAction()
{
    static const AmbushAction action;
    return action;
}

}  // namespace bot
