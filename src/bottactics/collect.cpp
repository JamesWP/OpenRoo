/* Collect: with gems needed and a route to them in the world the facts make,
 * the gems still needed, from the map, and on to the exit; or, where glides
 * may be had and the exit cannot be walked to, those to be had here, up to
 * the quota (to glide on, glide.cpp); or, where the exit is shut and a blast
 * may open it, the quota alone (blast.cpp then opens the way).  By the route optimiser's route
 * (board.h), ending at its last gem.  Done as a walk to the route's next gem, or another of the
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

    /* Whether the player has bombs, or a pickup of them is not taken. */
    static bool bombsToBe(const Facts &f, const Board &board)
    {
        if (f.bombs > 0)
            return true;
        const std::vector<Pickup> &pickups = board.pickups();
        for (size_t i = 0; i < pickups.size() && i < 32; i++)
            if (pickups[i].kind == Pickup::Bombs && !(f.pickups & (1u << i)))
                return true;
        return false;
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
        } else if (board.time(f.changes, f.at, board.exit()) < 0 &&
                   (f.changes & board.allBlocks()) != board.allBlocks() && bombsToBe(f, board)) {
            // The exit is shut, and a blast may yet open it (into a pit the
            // gems cannot be had from, as like as not): the quota first.
            const GemRoute &g = board.gather(f.changes, f.at, f.taken, f.gemsNeeded);
            if (!g.found || (int)g.gems.size() < f.gemsNeeded)
                return;
            Successor n{f, {}};
            n.facts.gemsNeeded = 0;
            for (const Step &gem : g.gems)
                n.facts.taken |= board.gemBit(gem);
            out.push_back(along(n, g));
        } else if (board.glidesToBe()) {
            // No way on to the exit from here: what this part of the level
            // gives, up to the quota, to glide on.
            const GemRoute &g = board.gather(f.changes, f.at, f.taken, f.gemsNeeded);
            if (!g.found)
                return;
            Successor n{f, {}};
            n.facts.gemsNeeded -= (int)g.gems.size();
            for (const Step &gem : g.gems)
                n.facts.taken |= board.gemBit(gem);
            out.push_back(along(n, g));
        }
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
