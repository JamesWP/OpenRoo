/* Collect: with gems needed, the gems still needed from the map in the world
 * the facts make: on the route on to the exit if there is one; else (the exit
 * shut, for a later step to open, or out of walking reach) the quota alone,
 * or, where glides may be had, those to be had here, up to the quota (to
 * glide on, glide.cpp).  By the route optimiser's route (board.h), ending at
 * its last gem.  Done as a walk to the route's next gem, or another of the
 * nearest gems (costed with the whole route still to do). */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class CollectAction : public Action {
    /* The successor at the route's end, its step the route. */
    static Successor along(Successor n, const GemRoute &r)
    {
        n.facts.at   = {r.gems.back().u, r.gems.back().v, 0};
        n.step.end   = n.facts.at;
        n.step.heads = r.gems.front();
        n.step.ms    = r.toLast;
        return n;
    }

public:
    const char *name() const override { return "collect"; }

    void expand(const Facts &f, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.gemsNeeded <= 0)
            return;
        const GemRoute &r = board.gemRoute(f.changes, f.at, f.gemsNeeded, f.taken);
        if (r.found) {
            Successor n{f, {}};
            n.facts.gemsNeeded = 0;
            out.push_back(along(n, r));
            return;
        }
        // No way on to the exit from here: the quota for another step to go
        // on from, or what this part of the level gives, to glide on.
        const GemRoute &g = board.gather(f.changes, f.at, f.taken, f.gemsNeeded);
        if (!g.found || ((int)g.gems.size() < f.gemsNeeded && !board.glidesToBe()))
            return;
        Successor n{f, {}};
        n.facts.gemsNeeded -= (int)g.gems.size();
        for (const Step &gem : g.gems)
            n.facts.taken |= board.gemBit(gem);
        out.push_back(along(n, g));
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        offerWalks(s, targets(s, plan, plan.steps.front().heads, board.gems(0), &board),
                   "gem", true, out);
    }
};

}  // namespace

const Action &collectAction()
{
    static const CollectAction action;
    return action;
}

}  // namespace bot
