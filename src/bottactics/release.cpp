/* Release: with a bomb, from a site whose blast spends a block that pens a
 * follower in (board.h, Release): the block spent, a bomb less, and the
 * catchers the follower then reaches counted gone, since it bombs them.  Its
 * effect is an estimate (the follower takes a while, and the catchers
 * move); only the blast itself is checked.  Done as a blast is (drops.h). */
#include "action.h"
#include <algorithm>
#include "drops.h"

namespace bot {

namespace {

/* As a blast's: the fuse and the blast's clearing. */
const double RELEASE_MS = 2600;

class ReleaseAction : public Action {
public:
    const char *name() const override { return "release"; }

    /* The cheapest site for each release. */
    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.bombs <= 0 || f.foes <= f.carriers)
            return;
        for (const Release &r : board.releases()) {
            if (f.changes & r.gate)
                continue;
            double best = -1;
            Site at;
            for (const Site &site : board.sites(f.changes)) {
                if (!(site.hits & r.gate))
                    continue;
                const double t = board.time(f.changes, f.at, {site.cell.u, site.cell.v});
                if (t < 0)
                    continue;
                const double cost = t + board.turnMs(f.at, site.cell.facing) + RELEASE_MS;
                if (best < 0 || cost < best) {
                    best = cost;
                    at = site;
                }
            }
            if (best < 0)
                continue;
            Successor n{f, {}};
            n.facts.changes |= at.hits;
            n.facts.bombs--;
            n.facts.unused = false;
            n.facts.foes = std::max(f.carriers, f.foes - r.foes);
            n.facts.at = at.cell;
            n.step.end   = at.cell;
            n.step.heads = {at.cell.u, at.cell.v};
            n.step.ms    = best;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &, const Plan &plan,
               std::vector<Option> &out) const override
    {
        offerBlast(s, plan.steps.front().end, out);
    }
};

}  // namespace

const Action &releaseAction()
{
    static const ReleaseAction action;
    return action;
}

}  // namespace bot
